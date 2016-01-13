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

#include "utils/logger.h"

#include "async/Base.h"

namespace async
{

/**
 * Allocates a buffer using MPI allocation
 */
class MPIAllocator
{
public:
	template<typename T>
	static T* alloc(size_t size)
	{
		if (size == 0)
			return 0;

		T* ptr;
		MPI_Alloc_mem(size*sizeof(T), MPI_INFO_NULL, &ptr);
		return ptr;
	}

	template<typename T>
	static void dealloc(T* mem)
	{
		MPI_Free_mem(mem);
	}
};


/**
 * Asynchronous call via MPI
 */
template<class Executor, typename InitParameter, typename Parameter>
class AsyncMPI : public Base<Executor, MPIAllocator>
{
private:
	/** True of this task is an executor */
	bool m_isExecutor;

	/** The group local communicator */
	MPI_Comm m_groupComm;

	/** The rank of in the executor group */
	int m_groupRank;

	/** The size of the executor group */
	int m_groupSize;

	/** Buffer offsets (only on the executor rank) */
	unsigned long* m_bufferOffsets;

	/** Current position of the buffer (only on the exuecutor rank) */
	size_t* m_bufferPos;

public:
	AsyncMPI()
		: m_isExecutor(false),
		  m_groupComm(MPI_COMM_NULL),
		  m_groupRank(0), m_groupSize(0),
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
		m_isExecutor = rank % numExecTasks == m_groupSize-1;
	}

	bool isExecutor() const
	{
		return m_isExecutor;
	}

	void init(Executor &executor, size_t bufferSize)
	{
		int executorRank = m_groupSize-1;

		// Compute buffer size and offsets
		unsigned long bs = bufferSize; // Use an MPI compatible datatype
		if (m_isExecutor)
			m_bufferOffsets = new unsigned long[m_groupSize];
		MPI_Gather(&bs, 1, MPI_UNSIGNED_LONG, m_bufferOffsets,
				1, MPI_UNSIGNED_LONG, executorRank, m_groupComm);

		bufferSize = 0;
		if (m_isExecutor) {
			for (int i = 0; i < m_groupSize-1; i++) {
				unsigned long bufSize = m_bufferOffsets[i];
				m_bufferOffsets[i] = bufferSize;
				bufferSize += bufSize;
			}
		}

		Base<Executor, MPIAllocator>::init(executor, bufferSize);

		// Final initialization on the executor
		if (m_isExecutor) {
			m_bufferPos = new size_t[m_groupSize-1];
			resetBufferPos();
		}
	}

	/**
	 * Should be called on the executor to start the execution loop
	 */
	void executorLoop()
	{
		if (!m_isExecutor)
			return;

		// Tell everybody that we are ready
		MPI_Barrier(m_groupComm);

		// Test for one init call
		MPI_Status status;
		MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, m_groupComm, &status);

		if (status.MPI_TAG == INIT_TAG) {
			InitParameter initParameters;

			MPI_Recv(&initParameters, sizeof(InitParameter), MPI_CHAR,
					status.MPI_SOURCE, INIT_TAG, m_groupComm, MPI_STATUS_IGNORE);

			Base<Executor, MPIAllocator>::executor().execInit(initParameters);

			MPI_Barrier(m_groupComm);
		}

		while (true) {
			unsigned int readyTasks = 0;

			Parameter parameters;

			while (readyTasks < m_groupSize-1) {
				MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, m_groupComm, &status);

				char* buf;
				int size;

				switch (status.MPI_TAG) {
				case INIT_TAG:
					logError() << "Initialization can only be send once at the beginning";
					break;
				case BUFFER_TAG:
					// Compute the buffer position
					buf = Base<Executor, MPIAllocator>::_buffer()
						+ m_bufferOffsets[status.MPI_SOURCE] + m_bufferPos[status.MPI_SOURCE];

					// Get the size that is received
					MPI_Get_count(&status, MPI_CHAR, &size);

					// Receive the message
					MPI_Recv(buf, size, MPI_CHAR, status.MPI_SOURCE, BUFFER_TAG, m_groupComm, MPI_STATUS_IGNORE);

					// Update the buffer position for the task
					m_bufferPos[status.MPI_SOURCE] += size;
					break;
				case PARAM_TAG:
					MPI_Recv(&parameters, sizeof(Parameter), MPI_CHAR,
							status.MPI_SOURCE, PARAM_TAG, m_groupComm, MPI_STATUS_IGNORE);
					readyTasks++;
					break;
				case EXIT_TAG:
					MPI_Recv(0L, 0, MPI_CHAR, status.MPI_SOURCE, EXIT_TAG, m_groupComm,
							MPI_STATUS_IGNORE);
					// Kill the executor
					return;
				}
			}

			Base<Executor, MPIAllocator>::executor().exec(parameters);

			// Reset buffer positions
			resetBufferPos();

			// Tell all other tasks that we are done
			MPI_Barrier(m_groupComm);
		}
	}

	/**
	 * Wait for an asynchronous call to finish
	 */
	void wait()
	{
		// Wait for the call to finish
		MPI_Barrier(m_groupComm);
	}

	void fillBuffer(const void* buffer, size_t size)
	{
		MPI_Send(const_cast<void*>(buffer), size, MPI_CHAR, m_groupSize-1,
				BUFFER_TAG, m_groupComm);
	}

	/**
	 * @warning Only parameter from rank 0 of the group will be considered
	 */
	void initCall(const InitParameter &parameters)
	{
		if (m_groupRank == 0) {
			// For simplification, we send the Parameter struct as a buffer
			// TODO find a nice way for hybrid systems
			MPI_Send(const_cast<InitParameter*>(&parameters), sizeof(InitParameter),
					MPI_CHAR, m_groupSize-1, INIT_TAG, m_groupComm);
		}
	}

	/**
	 * @warning Only the parameter from the last task will be considered
	 */
	void call(const Parameter &parameters)
	{
		// For simplification, we send the Parameter struct as a buffer
		// TODO find a nice way for hybrid systems
		MPI_Send(const_cast<Parameter*>(&parameters), sizeof(Parameter),
				MPI_CHAR, m_groupSize-1, PARAM_TAG, m_groupComm);
	}

	void finalize()
	{
		if (!Base<Executor, MPIAllocator>::finalize())
			return;

		if (m_groupRank == 0)
			MPI_Send(0L, 0, MPI_CHAR, m_groupSize-1, EXIT_TAG, m_groupComm);
	}

private:
	void resetBufferPos()
	{
		for (int i = 0; i < m_groupSize-1; i++)
			m_bufferPos[i] = 0;
	}

private:
	static const int INIT_TAG = 0;
	static const int BUFFER_TAG = 1;
	static const int PARAM_TAG = 2;
	static const int EXIT_TAG = 3;
};

}

#endif // ASYNC_ASYNCMPI_H
