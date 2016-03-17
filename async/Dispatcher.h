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

#ifndef ASYNC_DISPATCHER_H
#define ASYNC_DISPATCHER_H

#ifdef USE_ASYNC_MPI
#include "async/as/MPIScheduler.h"
#endif // USE_ASYNC_MPI

#include "ModuleBase.h"

namespace async
{

class Dispatcher
{
private:
#ifdef USE_ASYNC_MPI
	async::as::MPIScheduler m_scheduler;
#endif // USE_ASYNC_MPI

#ifdef USE_MPI
	MPI_Comm m_comm;
#endif // USE_MPI

	unsigned int m_groupSize;

public:
	Dispatcher() :
#ifdef USE_MPI
			m_comm(MPI_COMM_NULL),
#endif // USE_MPI
			m_groupSize(64)
	{
	}

	~Dispatcher()
	{
#ifdef USE_ASYNC_MPI
		// Delete all modules so we can create a new dispatcher
		// probably only important for testing
		ModuleBase::modules().clear();
#endif // USE_ASYNC_MPI
	}

#ifdef USE_MPI
	void setCommunicator(MPI_Comm comm)
	{
		m_comm = comm;
	}
#endif

	/**
	 * The group size for the MPI async mode
	 *
	 * @param groupSize The group size (excl. the MPI executor)
	 */
	void setGroupSize(unsigned int groupSize)
	{
		m_groupSize = groupSize;
	}

	/**
	 * This function will not return for MPI executors until all executors have been
	 * finalized. The function has to be called after all async {@link Module}s have
	 * been created.
	 *
	 * @return False if this rank is an MPI executor that does not contribute to the
	 *  computation.
	 */
	bool init()
	{
#ifdef USE_ASYNC_MPI
		const std::vector<ModuleBase*>& modules = ModuleBase::modules();
		// Set the scheduler for all modules
		for (std::vector<ModuleBase*>::const_iterator i = modules.begin();
				i != modules.end(); i++)
			(*i)->setScheduler(m_scheduler);

		// Initialize the scheduler
		m_scheduler.setCommunicator(m_comm, m_groupSize);

		if (m_scheduler.isExecutor()) {
			// Initialize the executor modules
			for (std::vector<ModuleBase*>::const_iterator i = modules.begin();
					i != modules.end(); i++)
				(*i)->setUp();

			// Run the executor loop
			m_scheduler.loop();

			// Finalize the executor modules
			for (std::vector<ModuleBase*>::const_iterator i = modules.begin();
					i != modules.end(); i++)
				(*i)->tearDown();
			return false;
		}
#endif // USE_ASYNC_MPI

		return true;
	}
};

}

#endif // ASYNC_DISPATCHER_H
