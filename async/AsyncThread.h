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

#ifndef ASYNC_ASYNCTHREAD_H
#define ASYNC_ASYNCTHREAD_H

#include <cstring>

#include "async/AsyncThreadBase.h"

namespace async
{

/**
 * Asynchronous call via pthreads
 */
template<class Executor, typename Parameter>
class AsyncThread : public AsyncThreadBase<Executor, Parameter>
{
private:
	/** The current buffer position */
	size_t m_bufferPos;

public:
	AsyncThread()
		: m_bufferPos(0)
	{
	}
	
	~AsyncThread()
	{
	}

	/**
	 * Will always return <code>true</code> for threads. Only
	 * for compatibility with the MPI mode.
	 */
	bool isExecutor() const
	{
		return true;
	}

	void fillBuffer(const void* buffer, size_t size)
	{
		memcpy(Base<Executor>::_buffer()+m_bufferPos, buffer, size);
		m_bufferPos += size;
	}
	
	void call(const Parameter &parameters)
	{
		AsyncThreadBase<Executor, Parameter>::call(parameters);

		// Reset the buffer position
		m_bufferPos = 0;
	}
};

}

#endif // ASYNC_ASYNCTHREAD_H
