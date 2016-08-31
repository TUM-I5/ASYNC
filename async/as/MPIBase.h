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

#ifndef ASYNC_AS_MPIBASE_H
#define ASYNC_AS_MPIBASE_H

#include <mpi.h>

#include <cassert>
#include <cstring>
#include <vector>

#include "async/Config.h"
#include "ThreadBase.h"
#include "MPIScheduler.h"

namespace async
{

namespace as
{

/**
 * Asynchronous call via MPI
 *
 * @warning This class behaves very different depending on executor and non-executor
 *  ranks. Some variables are only available on non-executors while others are only
 *  available on executors.
 */
template<class Executor, typename InitParameter, typename Parameter>
class MPIBase : public ThreadBase<Executor, InitParameter, Parameter>, private Scheduled
{
private:
	/**
	 * Buffer description on the executor
	 */
	struct ExecutorBuffer
	{
		/** Offsets for all tasks */
		const unsigned long* offsets;

		/** Next writing position for all tasks */
		size_t* positions;
	};

private:
	/** The max amount that should be transfered in a single MPI send operation */
	const size_t m_maxSend;

	/** The scheduler */
	MPIScheduler* m_scheduler;

	/** The identifier for this async call */
	int m_id;

	/** Number of buffer chunks (only on the executor rank) */
	unsigned int m_numBufferChunks;

	/** Buffer description on executors */
	std::vector<ExecutorBuffer> m_executorBuffers;

	/** Current position of the buffer on non-executors */
	std::vector<size_t> m_bufferPos;

public:
	MPIBase()
		: m_maxSend(async::Config::maxSend()),
		  m_scheduler(0L),
		  m_id(0),
		  m_numBufferChunks(0)
	{
	}

	~MPIBase()
	{
		finalize();
	}

	void setScheduler(MPIScheduler &scheduler)
	{
		m_scheduler = &scheduler;
	}

	/**
	 * @param executor
	 */
	void setExecutor(Executor &executor)
	{
		// Initialization on the executor
		if (m_scheduler->isExecutor())
			ThreadBase<Executor, InitParameter, Parameter>::setExecutor(executor);

		// Add this to the scheduler
		m_id = m_scheduler->addScheduled(this);
	}

	/**
	 */
	unsigned int addBuffer(const void* buffer, size_t size)
	{
		assert(m_scheduler);
		assert(!m_scheduler->isExecutor());

		m_scheduler->addBuffer(m_id,
			Base<Executor, InitParameter, Parameter>::numBuffers());

		return _addBuffer(buffer, size);
	}

	const void* buffer(unsigned int id) const
	{
		if (m_scheduler->isExecutor())
			return ThreadBase<Executor, InitParameter, Parameter>::buffer(id);

		return 0L;
	}

	/**
	 * Wait for an asynchronous call to finish
	 */
	void wait()
	{
		// Wait for the call to finish
		m_scheduler->wait(m_id);

		resetBufferPosition();
	}

	/**
	 * @warning Only the parameter from one task will be considered
	 */
	void callInit(const InitParameter &parameters)
	{
		m_scheduler->sendInitParam(m_id, parameters);

		resetBufferPosition();
	}

	void finalize()
	{
		if (!Base<Executor, InitParameter, Parameter>::_finalize())
			return;

		if (!m_scheduler->isExecutor())
			m_scheduler->sendFinalize(m_id);
	}

protected:
	size_t maxSend() const
	{
		return m_maxSend;
	}

	int id() const
	{
		return m_id;
	}

	MPIScheduler& scheduler()
	{
		return *m_scheduler;
	}

	const MPIScheduler& scheduler() const
	{
		return *m_scheduler;
	}

	/**
	 * The current position of a buffer on non-executors
	 */
	size_t bufferPos(unsigned int id) const
	{
		return m_bufferPos[id];
	}

	/**
	 * Increment the current buffer position on a non-executor
	 */
	void incBufferPos(unsigned int id, size_t increment)
	{
		m_bufferPos[id] += increment;
	}

private:
	/**
	 * Reset the buffer position on non-executors
	 */
	void resetBufferPosition()
	{
		for (std::vector<size_t>::iterator it = m_bufferPos.begin();
			it != m_bufferPos.end(); it++)
			*it = 0;
	}

	unsigned int paramSize() const
	{
		return std::max(sizeof(InitParameter), sizeof(Parameter));
	}

	unsigned int numBufferChunks() const
	{
		return m_numBufferChunks;
	}

	void _addBuffer()
	{
		_addBuffer(0L, 0);
	}

	unsigned int _addBuffer(const void* origin, unsigned long size)
	{
		int executorRank = m_scheduler->groupSize()-1;

		// Compute buffer size and offsets
		unsigned long* bufferOffsets = 0L;
		if (m_scheduler->isExecutor()) {
			assert(origin == 0L);
			assert(size == 0);

			bufferOffsets = new unsigned long[m_scheduler->groupSize()];
		}

		MPI_Gather(&size, 1, MPI_UNSIGNED_LONG, bufferOffsets,
				1, MPI_UNSIGNED_LONG, executorRank, m_scheduler->privateGroupComm());

		if (m_scheduler->isExecutor()) {
			// Compute total size and offsets
			size = 0;
			for (int i = 0; i < m_scheduler->groupSize()-1; i++) {
				// Compute offsets from the size
				unsigned long bufSize = bufferOffsets[i];
				bufferOffsets[i] = size;

				// Increment the total buffer size
				size += bufSize;

				// Increment the number of buffer chunks
				m_numBufferChunks += (bufSize + m_maxSend - 1) / m_maxSend;
			}

			// Create the buffer
			ThreadBase<Executor, InitParameter, Parameter>::addBuffer(0L, size);

			ExecutorBuffer executorBuffer;
			executorBuffer.offsets = bufferOffsets;

			// Initialize the current position
			executorBuffer.positions = new size_t[m_scheduler->groupSize()-1];
			memset(executorBuffer.positions, 0, (m_scheduler->groupSize()-1) * sizeof(size_t));

			m_executorBuffers.push_back(executorBuffer);
		} else {
			m_bufferPos.push_back(0);
		}

		return Base<Executor, InitParameter, Parameter>::numBuffers()-1;
	}

	void _execInit(const void* paramBuffer)
	{
		const InitParameter* param = reinterpret_cast<const InitParameter*>(paramBuffer);
		Base<Executor, InitParameter, Parameter>::executor().execInit(*param);

		resetBufferPositionOnEexecutor();
	}

	void* getBufferPos(unsigned int id, int rank, int size)
	{
		assert(rank < m_scheduler->groupSize()-1);
		assert(bufferOffset(id, rank)+size <= (Base<Executor, InitParameter, Parameter>::bufferSize(id)));

		void* buf = Base<Executor, InitParameter, Parameter>::_buffer(id)+bufferOffset(id, rank);
		m_executorBuffers[id].positions[rank] += size;
		return buf;
	}

	void _exec(const void* paramBuffer)
	{
		const Parameter* param = reinterpret_cast<const Parameter*>(paramBuffer);
		ThreadBase<Executor, InitParameter, Parameter>::call(*param);
	}

	void _wait()
	{
		ThreadBase<Executor, InitParameter, Parameter>::wait();

		resetBufferPositionOnEexecutor();
	}

	void _finalize()
	{
		for (typename std::vector<ExecutorBuffer>::iterator it = m_executorBuffers.begin();
				it != m_executorBuffers.end(); it++) {
			delete [] it->offsets;
			it->offsets = 0L;
			delete [] it->positions;
			it->positions = 0L;
		}

		ThreadBase<Executor, InitParameter, Parameter>::finalize();
	}

	/**
	 * @return The current offset on the buffer on the executor
	 */
	size_t bufferOffset(unsigned int id, int rank)
	{
		return m_executorBuffers[id].offsets[rank]+m_executorBuffers[id].positions[rank];
	}

	/**
	 * Reset buffer positions
	 *
	 * Should only be called on the executor.
	 */
	void resetBufferPositionOnEexecutor()
	{
		for (typename std::vector<ExecutorBuffer>::iterator it = m_executorBuffers.begin();
				it != m_executorBuffers.end(); it++)
			memset(it->positions, 0, (m_scheduler->groupSize()-1) * sizeof(size_t));
	}
};

}

}

#endif // ASYNC_AS_MPIBASE_H
