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

#ifndef ASYNC_ASYNCMPISCHEDULER_H
#define ASYNC_ASYNCMPISCHEDULER_H

#include <mpi.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "utils/logger.h"

namespace async
{

template<class Executor, typename InitParameter, typename Parameter>
class AsyncMPI;
class AsyncMPIScheduler;

class Scheduled
{
	friend class AsyncMPIScheduler;
protected:
	virtual ~Scheduled()
	{ }

private:
	virtual void execInit(const void* parameter) = 0;

	virtual void* getBufferPos(int rank, int size) = 0;

	virtual void exec(const void* parameter) = 0;

	/**
	 * Wait on the executor for the call to finish
	 */
	virtual void waitOnExecutor() = 0;

	virtual void finalizeOnExecutor() = 0;
};

/**
 * Scheduler for asynchronous MPI calls
 *
 * @warning Only one instance of this class should be created
 */
class AsyncMPIScheduler
{
	template<class Executor, typename InitParameter, typename Parameter>
	friend class AsyncMPI;
private:
	/** The group local communicator */
	MPI_Comm m_groupComm;

	/** The rank of in the executor group */
	int m_groupRank;

	/** The size of the executor group */
	int m_groupSize;

	/** True of this task is an executor */
	bool m_isExecutor;

	/** All async objects */
	std::vector<Scheduled*> m_asyncCalls;

	/** The maximum size (in bytes) of the parameter structure */
	unsigned int m_maxParamSize;

	/** Class is finalized? */
	bool m_finalized;

public:
	AsyncMPIScheduler()
		: m_groupComm(MPI_COMM_NULL),
		  m_groupRank(0), m_groupSize(0),
		  m_isExecutor(false),
		  m_maxParamSize(0),
		  m_finalized(false)
	{
	}

	~AsyncMPIScheduler()
	{
		finalize();
	}

	/**
	 * Set the MPI configuration.
	 *
	 * Has to be called before {@link init()}
	 *
	 * @param comm The MPI communicator that should be used
	 * @param numExecTask Number of executor tasks
	 */
	void setCommunicator(MPI_Comm comm, unsigned int numExecTasks)
	{
		int rank;
		MPI_Comm_rank(comm, &rank);

		// Create group communicator
		MPI_Comm_split(comm, rank / numExecTasks, 0, &m_groupComm);

		// Get group rank/size
		MPI_Comm_rank(m_groupComm, &m_groupRank);
		MPI_Comm_size(m_groupComm, &m_groupSize);

		// Is an executor?
		m_isExecutor = rank % static_cast<int>(numExecTasks) == m_groupSize-1;
	}

	bool isExecutor() const
	{
		return m_isExecutor;
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

		// Allocate buffer for parameters
		char* paramBuffer = new char[m_maxParamSize];

		// Number of finalized aync calls
		unsigned int finalized = 0;

		while (finalized < m_asyncCalls.size()) {
			MPI_Status status;
			int id, tag;

			//bool breakLoop = false;

			do {
				MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, m_groupComm, &status);

				id = status.MPI_TAG / NUM_TAGS;
				tag = status.MPI_TAG % NUM_TAGS;

				if (id > static_cast<int>(m_asyncCalls.size()) || m_asyncCalls[id] == 0L)
					logError() << "ASYNC: Invalid id" << id << "received";

				//char* buf;
				int size;

				switch (tag) {
				case INIT_TAG:
				case PARAM_TAG:
					MPI_Get_count(&status, MPI_CHAR, &size);
					MPI_Recv(paramBuffer, size, MPI_CHAR,
							status.MPI_SOURCE, status.MPI_TAG, m_groupComm, MPI_STATUS_IGNORE);

					readyTasks[id]++;
					break;
				case BUFFER_TAG:
					// Get the size that is received
					MPI_Get_count(&status, MPI_CHAR, &size);

					// Receive the message
					MPI_Recv(m_asyncCalls[id]->getBufferPos(status.MPI_SOURCE, size),
							size, MPI_CHAR, status.MPI_SOURCE, status.MPI_TAG, m_groupComm,
							MPI_STATUS_IGNORE);

					break;
				case WAIT_TAG:
				case FINALIZE_TAG:
					MPI_Recv(0L, 0, MPI_CHAR, status.MPI_SOURCE, status.MPI_TAG, m_groupComm,
							MPI_STATUS_IGNORE);

					readyTasks[id]++;
					break;
				default:
					logError() << "ASYNC: Unknown tag" << tag << "received";
				}

			} while (static_cast<int>(readyTasks[id]) < m_groupSize-1);

			switch (tag) {
			case INIT_TAG:
				MPI_Barrier(m_groupComm);
				m_asyncCalls[id]->execInit(paramBuffer);
				break;
			case PARAM_TAG:
				MPI_Barrier(m_groupComm);
				m_asyncCalls[id]->exec(paramBuffer);
				break;
			case WAIT_TAG:
				m_asyncCalls[id]->waitOnExecutor();
				MPI_Barrier(m_groupComm);
				break;
			case FINALIZE_TAG:
				// Forget the async call
				m_asyncCalls[id]->finalizeOnExecutor();
				m_asyncCalls[id] = 0L;
				finalized++;
				break;
			default:
				logError() << "ASYNC: Unknown tag" << tag;
			}

			readyTasks[id] = 0;
		}

		delete [] readyTasks;
		delete [] paramBuffer;
	}

	void finalize()
	{
		if (m_finalized)
			return;

		if (m_groupComm != MPI_COMM_NULL)
			MPI_Comm_free(&m_groupComm);

		m_finalized = true;
	}

private:
	MPI_Comm groupComm() const
	{
		return m_groupComm;
	}

	int groupSize() const
	{
		return m_groupSize;
	}

	int addScheduled(Scheduled* scheduled, unsigned int initParamSize, unsigned int paramSize)
	{
		int id = m_asyncCalls.size();
		m_asyncCalls.push_back(scheduled);

		unsigned int maxSize = std::max(initParamSize, paramSize);
		m_maxParamSize = std::max(m_maxParamSize, maxSize);

		return id;
	}

	template<typename Parameter>
	void sendInitParam(int id, const Parameter &param)
	{
		// For simplification, we send the Parameter struct as a buffer
		// TODO find a nice way for hybrid systems
		MPI_Send(const_cast<Parameter*>(&param), sizeof(Parameter),
				MPI_CHAR, m_groupSize-1, id*NUM_TAGS+INIT_TAG, m_groupComm);

		MPI_Barrier(m_groupComm);
	}

	void sendBuffer(int id, const void* buffer, int size)
	{
		// Use synchronous send to avoid overtaking of message
		MPI_Ssend(const_cast<void*>(buffer), size, MPI_CHAR,
				m_groupSize-1, id*NUM_TAGS+BUFFER_TAG, m_groupComm);
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
				MPI_CHAR, m_groupSize-1, id*NUM_TAGS+PARAM_TAG, m_groupComm);

		MPI_Barrier(m_groupComm);
	}

	void wait(int id)
	{
		MPI_Send(0L, 0, MPI_CHAR, m_groupSize-1,
				id*NUM_TAGS+WAIT_TAG, m_groupComm);

		// Wait for the return of the async call
		MPI_Barrier(m_groupComm);
	}

	void sendFinalize(int id)
	{
		MPI_Send(0L, 0, MPI_CHAR, m_groupSize-1,
				id*NUM_TAGS+FINALIZE_TAG, m_groupComm);
	}

private:
	static const int INIT_TAG = 0;
	static const int BUFFER_TAG = 1;
	static const int PARAM_TAG = 2;
	static const int WAIT_TAG = 3;
	static const int FINALIZE_TAG = 4;
	static const int NUM_TAGS = 5;
};

}

#endif // ASYNC_ASYNCMPISCHEDULER_H
