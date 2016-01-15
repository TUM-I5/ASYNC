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

#ifndef ASYNC_SYNC_H
#define ASYNC_SYNC_H

#include "async/Base.h"

namespace async
{

/**
 * Asynchronous call via pthreads
 */
template<class Executor, typename Parameter>
class AsyncSync : public Base<Executor>
{
public:
	AsyncSync()
	{
	}

	~AsyncSync()
	{
		finalize();
	}

	/**
	 * Will always return <code>true</code> for threads. Only
	 * relevant in MPI mode.
	 */
	bool isExecutor() const
	{
		return true;
	}

	void init(Executor &executor, size_t bufferSize)
	{
		Base<Executor>::init(executor, 0);
	}

	/**
	 * Does nothing (call has already finished because it is synchronous)
	 */
	void wait()
	{
	}

	void fillBuffer(const void* buffer, size_t size)
	{
	}

	void call(const Parameter &parameters)
	{
		Base<Executor>::executor().exec(parameters);
	}

	void finalize()
	{
		Base<Executor>::finalize();
	}
};

}

#endif // ASYNC_SYNC_H
