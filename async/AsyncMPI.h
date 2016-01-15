/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 *
 * @copyright Copyright (c) 2016, Technische Universitaet Muenchen.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *
 *  3. Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ASYNC_ASYNCMPI_H
#define ASYNC_ASYNCMPI_H

#include <mpi.h>

#include <algorithm>
#include <cassert>
#include <cstring>

#include "async/AsyncThreadBase.h"
#include "async/AsyncMPIScheduler.h"

namespace async
{

/**
 * Asynchronous call via MPI
 */
template<class Executor, typename InitParameter, typename Parameter>
class AsyncMPI : public AsyncThreadBase<Executor, Parameter>, private Scheduled
{
private:
	/** The scheduler */
	AsyncMPIScheduler &m_scheduler;

	/** The identifier for this async call */
	int m_id;

	/** Buffer offsets (only on the executor rank) */
	unsigned long* m_bufferOffsets;

	/** Current position of the buffer (only on the exuecutor rank) */
	size_t* m_bufferPos;

public:
	AsyncMPI(AsyncMPIScheduler &scheduler)
		: m_scheduler(scheduler),
		  m_id(0),
		  m_bufferOffsets(0L),
		  m_bufferPos(0L)
	{ }

	~AsyncMPI()
	{
		finalize();

		delete [] m_bufferOffsets;
		delete [] m_bufferPos;
	}

	/**
	 * @param executor
	 * @param bufferSize Should be 0 on the executor
	 */
	void init(Executor &executor, size_t bufferSize)
	{
		assert(bufferSize == 0 || !m_scheduler.isExecutor());

		int executorRank = m_scheduler.groupSize()-1;

		// Compute buffer size and offsets
		unsigned long bs = bufferSize; // Use an MPI compatible datatype
		if (m_scheduler.isExecutor())
			m_bufferOffsets = new unsigned long[m_scheduler.groupSize()];
		MPI_Gather(&bs, 1, MPI_UNSIGNED_LONG, m_bufferOffsets,
				1, MPI_UNSIGNED_LONG, executorRank, m_scheduler.groupComm());

		bufferSize = 0;
		if (m_scheduler.isExecutor()) {
			for (int i = 0; i < m_scheduler.groupSize()-1; i++) {
				unsigned long bufSize = m_bufferOffsets[i];
				m_bufferOffsets[i] = bufferSize;
				bufferSize += bufSize;
			}
		}

		// Final initialization on the executor
		if (m_scheduler.isExecutor()) {
			AsyncThreadBase<Executor, Parameter>::init(executor, bufferSize);

			m_bufferPos = new size_t[m_scheduler.groupSize()-1];
			memset(m_bufferPos, 0, (m_scheduler.groupSize()-1) * sizeof(size_t));
		}

		// Add this to the scheduler
		m_id = m_scheduler.addScheduled(this, sizeof(InitParameter), sizeof(Parameter));
	}

	/**
	 * Wait for an asynchronous call to finish
	 */
	void wait()
	{
		// Wait for the call to finish
		m_scheduler.wait(m_id);
	}

	void fillBuffer(const void* buffer, size_t size)
	{
		// We need to send the buffer in 1 GB chunks
		for (size_t done = 0; done < size; done += 1UL<<30) {
			size_t send = std::min(1UL<<30, size-done);

			m_scheduler.sendBuffer(m_id, static_cast<const char*>(buffer)+done, send);
		}
	}

	/**
	 * @warning Only the parameter from the last task will be considered
	 */
	void initCall(const InitParameter &parameters)
	{
		m_scheduler.sendInitParam(m_id, parameters);
	}

	/**
	 * @warning Only the parameter from the last task will be considered
	 */
	void call(const Parameter &parameters)
	{
		m_scheduler.sendParam(m_id, parameters);
	}

	void finalize()
	{
		if (!Base<Executor>::finalize())
			return;

		if (!m_scheduler.isExecutor())
			m_scheduler.sendFinalize(m_id);
	}

private:
	void execInit(const void* paramBuffer)
	{
		const InitParameter* param = reinterpret_cast<const InitParameter*>(paramBuffer);
		Base<Executor>::executor().execInit(*param);
	}

	void* getBufferPos(int rank, int size)
	{
		assert(rank < m_scheduler.groupSize()-1);

		void* buf = Base<Executor>::_buffer()+m_bufferOffsets[rank]+m_bufferPos[rank];
		m_bufferPos[rank] += size;
		return buf;
	}

	void exec(const void* paramBuffer)
	{
		const Parameter* param = reinterpret_cast<const Parameter*>(paramBuffer);
		AsyncThreadBase<Executor, Parameter>::call(*param);

		// Reset the buffer positions
		memset(m_bufferPos, 0, (m_scheduler.groupSize()-1) * sizeof(size_t));
	}

	void waitOnExecutor()
	{
		AsyncThreadBase<Executor, Parameter>::wait();
	}

	void finalizeOnExecutor()
	{
		AsyncThreadBase<Executor, Parameter>::finalize();
	}
};

}

#endif // ASYNC_ASYNCMPI_H
