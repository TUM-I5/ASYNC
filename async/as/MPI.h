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

#include "utils/env.h"

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
	/** The max amount that should be transfered in a single MPI send operation */
	const size_t m_maxSend;

	/** The scheduler */
	MPIScheduler* m_scheduler;

	/** The identifier for this async call */
	int m_id;

	/** Counter for buffer (w/o async call, we only create the buffers on the executor) */
	unsigned int m_numBuffers;

	/** Number of buffer chunks (only on the executor rank) */
	unsigned int m_numBufferChunks;

	/** Buffer offsets (only on the executor rank) */
	std::vector<const unsigned long*> m_bufferOffsets;

	/** Current position of the buffer (only on the exuecutor rank) */
	std::vector<size_t*> m_bufferPos;

	/** Buffer for the parameter (required for async calls) */
	Parameter m_paramBuffer;

	/** List of MPI requests (required for async call) */
	std::vector<MPI_Request> m_asyncRequests;

public:
	MPI()
		: m_maxSend(utils::Env::get<size_t>("ASYNC_MPI_MAX_SEND", 1UL<<30)),
		  m_scheduler(0L),
		  m_id(0),
		  m_numBuffers(0),
		  m_numBufferChunks(0)
	{
		// One request always required for the parameters
		m_asyncRequests.push_back(MPI_REQUEST_NULL);
	}

	~MPI()
	{
		finalize();

		for (std::vector<const unsigned long*>::const_iterator it = m_bufferOffsets.begin();
			it != m_bufferOffsets.end(); it++)
			delete [] *it;
		for (std::vector<size_t*>::const_iterator it = m_bufferPos.begin();
			it != m_bufferPos.end(); it++)
			delete [] *it;
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
		if (m_scheduler->useAyncCopy()) {
			// Wait for all requests first
			MPI_Waitall(m_asyncRequests.size(), &m_asyncRequests[0], MPI_STATUSES_IGNORE);
		}

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

		if (m_scheduler->useAyncCopy()) {
			// Only copy it to the local buffer
			assert(m_bufferPos[id][0]+size <= Base<Executor>::bufferSize(id));

			memcpy(Base<Executor>::_buffer(id)+m_bufferPos[id][0], buffer, size);
			m_bufferPos[id][0] += size;
			return;
		}

		// We need to send the buffer in 1 GB chunks
		for (size_t done = 0; done < size; done += m_maxSend) {
			size_t send = std::min(m_maxSend, size-done);

			m_scheduler->sendBuffer(m_id, id, static_cast<const char*>(buffer)+done, send);
		}
	}

	/**
	 * @warning Only the parameter from the last task will be considered
	 */
	void callInit(const InitParameter &parameters)
	{
		if (m_scheduler->useAyncCopy()) {
			assert(m_numBuffers == Base<Executor>::numBuffers());

			iSendAllBuffers();
		}

		m_scheduler->sendInitParam(m_id, parameters);
	}

	/**
	 * @warning Only the parameter from the last task will be considered
	 */
	void call(const Parameter &parameters)
	{
		if (m_scheduler->useAyncCopy()) {
			iSendAllBuffers();

			// Send parameters
			m_paramBuffer = parameters;
			m_asyncRequests.back() = m_scheduler->iSendParam(m_id, m_paramBuffer);
		} else {
			m_scheduler->sendParam(m_id, parameters);
		}
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

	unsigned int numBufferChunks() const
	{
		return m_numBufferChunks;
	}

	/**
	 * Sends all buffers asynchronously
	 *
	 * Should only be used in asynchronous copy mode
	 */
	void iSendAllBuffers()
	{
		assert(m_scheduler->useAyncCopy());

		unsigned int nextRequest = 0;

		// Send all buffers
		for (unsigned int i = 0; i < m_numBuffers; i++) {
			for (size_t done = 0; done < m_bufferPos[i][0]; done += m_maxSend) {
				size_t send = std::min(m_maxSend, m_bufferPos[i][0]-done);
				MPIRequest2 requests = m_scheduler->iSendBuffer(m_id, i, Base<Executor>::_buffer(i)+done, send);

				m_asyncRequests[nextRequest] = requests.r[0];
				m_asyncRequests[nextRequest+1] = requests.r[1];
				nextRequest += 2;
			}

			// Reset buffer position
			m_bufferPos[i][0] = 0;
		}

		assert(nextRequest == m_asyncRequests.size()-1);
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
			bufferSize = 0;
			for (int i = 0; i < m_scheduler->groupSize()-1; i++) {
				// Compute offsets from the size
				unsigned long bufSize = bufferOffsets[i];
				bufferOffsets[i] = bufferSize;

				// Increment the total buffer size
				bufferSize += bufSize;

				// Increment the number of buffer chunks
				m_numBufferChunks += (bufSize + m_maxSend - 1) / m_maxSend;
			}

			// Create the buffer
			unsigned int id = ThreadBase<Executor, Parameter>::addBuffer(bufferSize);
			assert(id == m_numBuffers); NDBG_UNUSED(id);

			// Initialize the current position
			m_bufferPos.push_back(new size_t[m_scheduler->groupSize()-1]);
			memset(m_bufferPos.back(), 0, (m_scheduler->groupSize()-1) * sizeof(size_t));
		} else if (m_scheduler->useAyncCopy()) {
			// Async copying requires a local buffer as well
			unsigned int id = Base<Executor>::addBuffer(bufferSize);
			assert(id == m_numBuffers); NDBG_UNUSED(id);

			// Initialize the current position
			m_bufferPos.push_back(new size_t[1]);
			m_bufferPos.back()[0] = 0;

			// Initialize the requests
			unsigned int requests = (bufferSize + m_maxSend - 1) / m_maxSend;
			m_asyncRequests.insert(m_asyncRequests.end(), requests*2, MPI_REQUEST_NULL);
		}

		// Increase the counter and return the id
		m_numBuffers++;
	}

	void _execInit(const void* paramBuffer)
	{
		const InitParameter* param = reinterpret_cast<const InitParameter*>(paramBuffer);
		Base<Executor>::executor().execInit(*param);

		resetBufferPosition();
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

		resetBufferPosition();
	}

	void _wait()
	{
		ThreadBase<Executor, Parameter>::wait();
	}

	void _finalize()
	{
		ThreadBase<Executor, Parameter>::finalize();
	}

	/**
	 * Reset buffer positions
	 *
	 * Should only be called on the executor.
	 */
	void resetBufferPosition()
	{
		for (unsigned int i = 0; i < Base<Executor>::numBuffers(); i++)
			memset(m_bufferPos[i], 0, (m_scheduler->groupSize()-1) * sizeof(size_t));
	}
};

}

}

#endif // ASYNC_AS_MPI_H
