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

#ifndef ASYNC_MODULE_H
#define ASYNC_MODULE_H

#ifdef USE_ASYNC_MPI
#include "async/as/MPI.h"
#else // USE_ASYNC_MPI
#ifdef USE_ASYNC_THREAD
#include "async/as/Thread.h"
#else // USE_ASYNC_THREAD
#include "async/as/Sync.h"
#endif // USE_ASYNC_THREAD
#endif // USE_ASYNC_MPI

#include "ModuleBase.h"

namespace async
{

template<class Executor, typename InitParam, typename Param>
class Module :
	public ModuleBase,
#ifdef USE_ASYNC_MPI
	public as::MPI<Executor, InitParam, Param>
#else // USE_ASYNC_MPI
#ifdef USE_ASYNC_THREAD
	public as::Thread<Executor, Param>
#else // USE_ASYNC_THREAD
	public as::Sync<Executor, Param>
#endif // USE_ASYNC_THREAD
#endif // USE_ASYNC_MPI
{
public:
#ifdef USE_ASYNC_MPI
#endif // USE_ASYNC_MPI

#ifdef USE_ASYNC_MPI
	void setScheduler(as::MPIScheduler &scheduler)
	{
		this->scheduler(scheduler);
	}
#else // USE_ASYNC_MPI
	/**
	 * Small function that makes sure that we can call <code>callInit</code>
	 * from all types of async modules.
	 */
	void callInit(const InitParam &parameters)
	{
		execInit(parameters);
	}

	/**
	 * Call finalize the executor as well
	 */
	void finalize()
	{
#ifdef USE_ASYNC_THREAD
		as::Thread<Executor, Param>::finalize();
#else // USE_ASYNC_THREAD
		as::Sync<Executor, Param>::finalize();
#endif // USE_ASYNC_THREAD

		tearDown();
	}
#endif // USE_ASYNC_MPI

protected:
	virtual void execInit(const InitParam &parameters) = 0;
};

}

#endif // ASYNC_MODULE_H
