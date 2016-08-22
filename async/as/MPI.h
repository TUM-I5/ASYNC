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

#ifndef ASYNC_AS_MPI_H
#define ASYNC_AS_MPI_H

#include <mpi.h>

#include <algorithm>
#include <cassert>
#include <cstring>

#include "ThreadBase.h"
#include "MPIScheduler.h"

namespace async
{

namespace as
{

/**
 * Asynchronous call via MPI
 */
template<class Executor, typename InitParameter, typename Parameter>
class MPI : public ThreadBase<Executor, Parameter>, private Scheduled
{
private:
	/** The scheduler */
	MPIScheduler* m_scheduler;

	/** The identifier for this async call */
	int m_id;

	/** Counter for buffer (we only create the buffers on the executor) */
	unsigned int m_numBuffers;

	/** Buffer offsets (only on the executor rank) */
	std::vector<const unsigned long*> m_bufferOffsets;

	/** Current position of the buffer (only on the exuecutor rank) */
	std::vector<size_t*> m_bufferPos;

public:
	MPI()
		: m_scheduler(0L),
		  m_id(0),
		  m_numBuffers(0)
	{ }

	~MPI()
	{
		finalize();

		for (unsigned int i = 0; i < Base<Executor>::numBuffers(); i++) {
			delete [] m_bufferOffsets[i];
			delete [] m_bufferPos[i];
		}
	}

	void scheduler(MPIScheduler &scheduler)
	{
		m_scheduler = &scheduler;
	}

	MPIScheduler& scheduler()
	{
		return *m_scheduler;
	}

	/**
	 * @param executor
	 */
	void setExecutor(Executor &executor)
	{
		// Initialization on the executor
		if (m_scheduler->isExecutor())
			ThreadBase<Executor, Parameter>::setExecutor(executor);

		// Add this to the scheduler
		m_id = m_scheduler->addScheduled(this);
	}

	/**
	 * @param bufferSize Should be 0 on the executor
	 */
	unsigned int addBuffer(size_t bufferSize)
	{
		assert(m_scheduler);
		assert(!m_scheduler->isExecutor());

		m_scheduler->addBuffer(m_id, bufferSize);

		_addBuffer(bufferSize);

		return m_numBuffers-1;
	}

	unsigned int numBuffers() const
	{
		return m_numBuffers;
	}

	/**
	 * Wait for an asynchronous call to finish
	 */
	void wait()
	{
		// Wait for the call to finish
		m_scheduler->wait(m_id);
	}

	/**
	 * @param id The id of the buffer
	 */
	void fillBuffer(unsigned int id, const void* buffer, size_t size)
	{
		if (buffer == 0L || size == 0)
			return;

		assert(id < numBuffers());

		// We need to send the buffer in 1 GB chunks
		for (size_t done = 0; done < size; done += 1UL<<30) {
			size_t send = std::min(1UL<<30, size-done);

			m_scheduler->sendBuffer(m_id, id, static_cast<const char*>(buffer)+done, send);
		}
	}

	/**
	 * @warning Only the parameter from the last task will be considered
	 */
	void callInit(const InitParameter &parameters)
	{
		m_scheduler->sendInitParam(m_id, parameters);
	}

	/**
	 * @warning Only the parameter from the last task will be considered
	 */
	void call(const Parameter &parameters)
	{
		m_scheduler->sendParam(m_id, parameters);
	}

	void finalize()
	{
		if (!Base<Executor>::finalize())
			return;

		if (!m_scheduler->isExecutor())
			m_scheduler->sendFinalize(m_id);
	}

private:
	unsigned int paramSize() const
	{
		return std::max(sizeof(InitParameter), sizeof(Parameter));
	}

	void _addBuffer(unsigned long bufferSize)
	{
		int executorRank = m_scheduler->groupSize()-1;

		// Compute buffer size and offsets
		unsigned long* bufferOffsets = 0L;
		if (m_scheduler->isExecutor()) {
			bufferOffsets = new unsigned long[m_scheduler->groupSize()];
			m_bufferOffsets.push_back(bufferOffsets);
		}
		MPI_Gather(&bufferSize, 1, MPI_UNSIGNED_LONG, bufferOffsets,
				1, MPI_UNSIGNED_LONG, executorRank, m_scheduler->privateGroupComm());

		if (m_scheduler->isExecutor()) {
			// Compute offsets from the size
			bufferSize = 0;
			for (int i = 0; i < m_scheduler->groupSize()-1; i++) {
				unsigned long bufSize = bufferOffsets[i];
				bufferOffsets[i] = bufferSize;
				bufferSize += bufSize;
			}

			// Create the buffer
			unsigned int id = ThreadBase<Executor, Parameter>::addBuffer(bufferSize);
			assert(id == m_numBuffers); NDBG_UNUSED(id);

			// Initialize the current position
			m_bufferPos.push_back(new size_t[m_scheduler->groupSize()-1]);
			memset(m_bufferPos.back(), 0, (m_scheduler->groupSize()-1) * sizeof(size_t));
		}

		// Increase the counter and return the id
		m_numBuffers++;
	}

	void _execInit(const void* paramBuffer)
	{
		const InitParameter* param = reinterpret_cast<const InitParameter*>(paramBuffer);
		Base<Executor>::executor().execInit(*param);
	}

	void* getBufferPos(unsigned int id, int rank, int size)
	{
		assert(rank < m_scheduler->groupSize()-1);
		assert(m_bufferOffsets[id][rank]+m_bufferPos[id][rank]+size
				<= Base<Executor>::bufferSize(id));

		void* buf = ThreadBase<Executor, Parameter>::_buffer(id)+
				m_bufferOffsets[id][rank]+m_bufferPos[id][rank];
		m_bufferPos[id][rank] += size;
		return buf;
	}

	void _exec(const void* paramBuffer)
	{
		const Parameter* param = reinterpret_cast<const Parameter*>(paramBuffer);
		ThreadBase<Executor, Parameter>::call(*param);

		// Reset the buffer positions
		for (unsigned int i = 0; i < Base<Executor>::numBuffers(); i++)
			memset(m_bufferPos[i], 0, (m_scheduler->groupSize()-1) * sizeof(size_t));
	}

	void _wait()
	{
		ThreadBase<Executor, Parameter>::wait();
	}

	void _finalize()
	{
		ThreadBase<Executor, Parameter>::finalize();
	}
};

}

}

#endif // ASYNC_AS_MPI_H
