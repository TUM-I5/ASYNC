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

#ifndef ASYNC_AS_MPISCHEDULER_H
#define ASYNC_AS_MPISCHEDULER_H

#include <mpi.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

#include "utils/logger.h"

#include "async/Config.h"

namespace async
{

namespace as
{

/**
 * A pair of MPI requests
 */
struct MPIRequest2
{
	MPI_Request r[2];
};

template<class Executor, typename InitParameter, typename Parameter>
class MPIBase;
template<class Executor, typename InitParameter, typename Parameter>
class MPI;
template<class Executor, typename InitParameter, typename Parameter>
class MPIAsync;
class MPIScheduler;

class Scheduled
{
	friend class MPIScheduler;
private:
	char* m_paramBuffer;

protected:
	Scheduled()
		: m_paramBuffer(0L)
	{ }

	virtual ~Scheduled()
	{
		delete [] m_paramBuffer;
	}

private:
	void allocParamBuffer()
	{
		m_paramBuffer = new char[paramSize()];
	}

	void* paramBuffer()
	{
		return m_paramBuffer;
	}

	const void* paramBuffer() const
	{
		return m_paramBuffer;
	}

	/**
	 * @return The maximum buffer size required to store init parameter
	 *  or parameter
	 */
	virtual unsigned int paramSize() const = 0;

	/**
	 * @return True if this class uses async copying
	 */
	virtual bool useAsyncCopy() const = 0;

	/**
	 * Returns the number of buffer chunks send to the executor
	 *
	 * This might differ from the number of buffers since large buffers
	 * have the be send in multiple iterations.
	 */
	virtual unsigned int numBufferChunks() const = 0;

	virtual void _addBuffer() = 0;

	virtual void _execInit(const void* parameter) = 0;

	virtual void* getBufferPos(unsigned int id, int rank, int size) = 0;

	virtual void _exec(const void* parameter) = 0;

	/**
	 * Wait on the executor for the call to finish
	 */
	virtual void _wait() = 0;

	virtual void _finalize() = 0;
};

/**
 * Scheduler for asynchronous MPI calls
 *
 * @warning Only one instance of this class should be created
 */
class MPIScheduler
{
	template<class Executor, typename InitParameter, typename Parameter>
	friend class MPIBase;
	template<class Executor, typename InitParameter, typename Parameter>
	friend class MPI;
	template<class Executor, typename InitParameter, typename Parameter>
	friend class MPIAsync;
private:
	/** The group local communicator (incl. the executor) */
	MPI_Comm m_privateGroupComm;

	/** The public group local communicator (excl. the executor) */
	MPI_Comm m_groupComm;

	/** The rank of in the executor group */
	int m_groupRank;

	/** The size of the executor group (incl. the executor) */
	int m_groupSize;

	/** True of this task is an executor */
	bool m_isExecutor;

	/** The "COMM_WORLD" communicator (communicator without executors) */
	MPI_Comm m_commWorld;

	/** All async objects */
	std::vector<Scheduled*> m_asyncCalls;

	/**
	 * A list of possible buffer ids
	 *
	 * With MPI_Isend we need to provide a memory location for buffer ids
	 * that is not on the stack. We can use this vector which contains:
	 * id[0] = 0, id[1] = 1, ...
	 */
	std::vector<unsigned int> m_heapBufferIds;

	/** Class is finalized? */
	bool m_finalized;

public:
	MPIScheduler()
		: m_privateGroupComm(MPI_COMM_NULL),
		  m_groupComm(MPI_COMM_NULL),
		  m_groupRank(0), m_groupSize(0),
		  m_isExecutor(false),
		  m_commWorld(MPI_COMM_NULL),
		  m_finalized(false)
	{
	}

	~MPIScheduler()
	{
		finalize();
	}

	/**
	 * Set the MPI configuration.
	 *
	 * Has to be called before {@link init()}
	 *
	 * @param comm The MPI communicator that should be used
	 * @param groupSize Number of ranks per group (excluding the executor)
	 */
	void setCommunicator(MPI_Comm comm, unsigned int groupSize)
	{
		int rank;
		MPI_Comm_rank(comm, &rank);

		// Create group communicator
		MPI_Comm_split(comm, rank / (groupSize+1), 0, &m_privateGroupComm);

		// Get group rank/size
		MPI_Comm_rank(m_privateGroupComm, &m_groupRank);
		MPI_Comm_size(m_privateGroupComm, &m_groupSize);

		// Is an executor?
		m_isExecutor = m_groupRank == m_groupSize-1;

		// Create the new comm world communicator
		MPI_Comm_split(comm, (m_isExecutor ? 1 : 0), 0, &m_commWorld);

		// Create the public group communicator (excl. the executor)
		MPI_Comm_split(m_privateGroupComm, (m_isExecutor ? MPI_UNDEFINED : 0), 0, &m_groupComm);
	}

	int groupRank() const
	{
		return m_groupRank;
	}

	bool isExecutor() const
	{
		return m_isExecutor;
	}

	MPI_Comm commWorld() const
	{
		return m_commWorld;
	}

	MPI_Comm groupComm() const
	{
		return m_groupComm;
	}

	/**
	 * Should be called on the executor to start the execution loop
	 */
	void loop()
	{
		if (!m_isExecutor)
			return;

		// Save ready tasks for each call
		unsigned int* readyTasks = new unsigned int[m_asyncCalls.size()];
		memset(readyTasks, 0, m_asyncCalls.size() * sizeof(unsigned int));

		unsigned int* asyncReadyTasks = new unsigned int[m_asyncCalls.size()];
		memset(asyncReadyTasks, 0, m_asyncCalls.size() * sizeof(unsigned int));

		// Distinguish between init and param tag (required for asynchronous copies)
		int* lastTag = new int[m_asyncCalls.size()];
		memset(lastTag, 0, m_asyncCalls.size() * sizeof(int));

		// Number of finalized aync calls
		unsigned int finalized = 0;

		while (finalized < m_asyncCalls.size()) {
			MPI_Status status;
			int id, tag;

			do {
				MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, m_privateGroupComm, &status);

				id = status.MPI_TAG / NUM_TAGS;
				tag = status.MPI_TAG % NUM_TAGS;

				if (id > static_cast<int>(m_asyncCalls.size()) || m_asyncCalls[id] == 0L)
					logError() << "ASYNC: Invalid id" << id << "received";

				char tmp;
				int size;
				unsigned int bufferId;
				void* buf;

				switch (tag) {
				case ADD_TAG:
					MPI_Recv(&tmp, 1, MPI_CHAR, status.MPI_SOURCE, status.MPI_TAG,
							m_privateGroupComm, MPI_STATUS_IGNORE);

					readyTasks[id]++;
					break;
				case INIT_TAG:
				case PARAM_TAG:
					MPI_Get_count(&status, MPI_CHAR, &size);
					MPI_Recv(m_asyncCalls[id]->paramBuffer(), size, MPI_CHAR,
							status.MPI_SOURCE, status.MPI_TAG, m_privateGroupComm, MPI_STATUS_IGNORE);

					lastTag[id] = tag;
					if (m_asyncCalls[id]->useAsyncCopy())
						asyncReadyTasks[id]++;
					else
						readyTasks[id]++;
					break;
				case BUFFER_TAG:
					// Select a buffer
					MPI_Recv(&bufferId, 1, MPI_UNSIGNED,
							status.MPI_SOURCE, status.MPI_TAG, m_privateGroupComm,
							MPI_STATUS_IGNORE);

					// Probe buffer receive from some source/tag
					MPI_Probe(status.MPI_SOURCE, status.MPI_TAG, m_privateGroupComm, &status);

					// Get the size that is received
					MPI_Get_count(&status, MPI_CHAR, &size);

					// Receive the buffer
					buf = m_asyncCalls[id]->getBufferPos(bufferId, status.MPI_SOURCE, size);
					MPI_Recv(buf, size, MPI_CHAR, status.MPI_SOURCE, status.MPI_TAG, m_privateGroupComm,
							MPI_STATUS_IGNORE);

					if (m_asyncCalls[id]->useAsyncCopy())
							asyncReadyTasks[id]++;
					break;
				case WAIT_TAG:
				case FINALIZE_TAG:
					MPI_Recv(0L, 0, MPI_CHAR, status.MPI_SOURCE, status.MPI_TAG, m_privateGroupComm,
							MPI_STATUS_IGNORE);

					readyTasks[id]++;
					break;
				default:
					logError() << "ASYNC: Unknown tag" << tag << "received";
				}

			} while ((static_cast<int>(readyTasks[id]) < m_groupSize-1)
				&& (asyncReadyTasks[id] < m_groupSize-1+m_asyncCalls[id]->numBufferChunks()));

			if (tag == BUFFER_TAG) {
				assert(lastTag[id] == INIT_TAG || lastTag[id] == PARAM_TAG);

				tag = lastTag[id];
			}

			switch (tag) {
			case ADD_TAG:
				MPI_Barrier(m_privateGroupComm);
				m_asyncCalls[id]->_addBuffer();
				break;
			case INIT_TAG:
				MPI_Barrier(m_privateGroupComm);
				m_asyncCalls[id]->_execInit(m_asyncCalls[id]->paramBuffer());
				break;
			case PARAM_TAG:
				if (!m_asyncCalls[id]->useAsyncCopy())
					MPI_Barrier(m_privateGroupComm);
				m_asyncCalls[id]->_exec(m_asyncCalls[id]->paramBuffer());
				break;
			case WAIT_TAG:
				m_asyncCalls[id]->_wait();
				// This barrier can probably be called before _wait()
				// Just leave it here to be 100% sure that everything is done
				// when the wait returns
				MPI_Barrier(m_privateGroupComm);
				break;
			case FINALIZE_TAG:
				// Forget the async call
				m_asyncCalls[id]->_finalize();
				m_asyncCalls[id] = 0L;
				finalized++;
				break;
			default:
				logError() << "ASYNC: Unknown tag" << tag;
			}

			if (static_cast<int>(readyTasks[id]) >= m_groupSize-1)
				readyTasks[id] = 0;
			else
				asyncReadyTasks[id] = 0;
		}

		delete [] readyTasks;
		delete [] asyncReadyTasks;
		delete [] lastTag;
	}

	void finalize()
	{
		if (m_finalized)
			return;

		if (m_privateGroupComm != MPI_COMM_NULL)
			MPI_Comm_free(&m_privateGroupComm);
		if (m_groupComm != MPI_COMM_NULL)
			MPI_Comm_free(&m_groupComm);

		m_finalized = true;
	}

private:
	MPI_Comm privateGroupComm() const
	{
		return m_privateGroupComm;
	}

	int groupSize() const
	{
		return m_groupSize;
	}

	int addScheduled(Scheduled* scheduled)
	{
		int id = m_asyncCalls.size();
		m_asyncCalls.push_back(scheduled);

		scheduled->allocParamBuffer();

		return id;
	}

	void addBuffer(int id, unsigned int bufferId)
	{
		if (bufferId >= m_heapBufferIds.size()) {
			assert(bufferId == m_heapBufferIds.size()); // IDs always increment by 1

			m_heapBufferIds.push_back(bufferId);
		}

		char tmp;
		MPI_Send(&tmp, 1, MPI_CHAR,
			m_groupSize-1, id*NUM_TAGS+ADD_TAG, m_privateGroupComm);

		MPI_Barrier(m_privateGroupComm);
	}

	void sendBuffer(int id, unsigned int bufferId, const void* buffer, int size)
	{
		// Select the buffer
		MPI_Send(&bufferId, 1, MPI_UNSIGNED,
				m_groupSize-1, id*NUM_TAGS+BUFFER_TAG, m_privateGroupComm);

		// Send the buffer (synchronous to avoid overtaking of other messages)
		MPI_Ssend(const_cast<void*>(buffer), size, MPI_CHAR,
				m_groupSize-1, id*NUM_TAGS+BUFFER_TAG, m_privateGroupComm);
	}

	MPIRequest2 iSendBuffer(int id, unsigned int bufferId, const void* buffer, int size)
	{
		MPIRequest2 requests;

		// Select the buffer
		MPI_Isend(&m_heapBufferIds[bufferId], 1, MPI_UNSIGNED,
				m_groupSize-1, id*NUM_TAGS+BUFFER_TAG, m_privateGroupComm,
				&requests.r[0]);

		// Send the buffer
		MPI_Isend(const_cast<void*>(buffer), size, MPI_CHAR,
				m_groupSize-1, id*NUM_TAGS+BUFFER_TAG, m_privateGroupComm,
				&requests.r[1]);

		return requests;
	}

	template<typename Parameter>
	void sendInitParam(int id, const Parameter &param)
	{
		// For simplification, we send the Parameter struct as a buffer
		// TODO find a nice way for hybrid systems
		MPI_Send(const_cast<Parameter*>(&param), sizeof(Parameter),
				MPI_CHAR, m_groupSize-1, id*NUM_TAGS+INIT_TAG, m_privateGroupComm);

		MPI_Barrier(m_privateGroupComm);
	}

	/**
	 * Send message to the executor task
	 */
	template<typename Parameter>
	void sendParam(int id, const Parameter &param)
	{
		// For simplification, we send the Parameter struct as a buffer
		// TODO find a nice way for hybrid systems
		MPI_Send(const_cast<Parameter*>(&param), sizeof(Parameter),
				MPI_CHAR, m_groupSize-1, id*NUM_TAGS+PARAM_TAG, m_privateGroupComm);

		MPI_Barrier(m_privateGroupComm);
	}

	/**
	 * Send message asynchronous to the executor task
	 */
	template<typename Parameter>
	MPI_Request iSendParam(int id, const Parameter &param)
	{
		MPI_Request request;

		MPI_Isend(const_cast<Parameter*>(&param), sizeof(Parameter), MPI_CHAR,
				m_groupSize-1, id*NUM_TAGS+PARAM_TAG, m_privateGroupComm,
				&request);

		return request;
	}

	void wait(int id)
	{
		MPI_Send(0L, 0, MPI_CHAR, m_groupSize-1,
				id*NUM_TAGS+WAIT_TAG, m_privateGroupComm);

		// Wait for the return of the async call
		MPI_Barrier(m_privateGroupComm);
	}

	void sendFinalize(int id)
	{
		MPI_Send(0L, 0, MPI_CHAR, m_groupSize-1,
				id*NUM_TAGS+FINALIZE_TAG, m_privateGroupComm);
	}

private:
	static const int ADD_TAG = 0;
	static const int INIT_TAG = 1;
	static const int BUFFER_TAG = 2;
	static const int PARAM_TAG = 3;
	static const int WAIT_TAG = 4;
	static const int FINALIZE_TAG = 5;
	static const int NUM_TAGS = FINALIZE_TAG + 1;
};

}

}

#endif // ASYNC_AS_MPISCHEDULER_H
