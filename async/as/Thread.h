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

#ifndef ASYNC_AS_THREAD_H
#define ASYNC_AS_THREAD_H

#include <cassert>
#include <cstring>

#include "ThreadBase.h"

namespace async
{

namespace as
{

/**
 * Asynchronous call via pthreads
 */
template<class Executor, typename Parameter>
class Thread : public ThreadBase<Executor, Parameter>
{
private:
	/** The current buffer position */
	std::vector<size_t> m_bufferPos;

public:
	Thread()
	{
	}
	
	~Thread()
	{
	}

	unsigned int addBuffer(size_t bufferSize)
	{
		unsigned int id = ThreadBase<Executor, Parameter>::addBuffer(bufferSize);
		m_bufferPos.push_back(0);

		return id;
	}

	/**
	 * Will always return <code>false</code> for threads. Only
	 * for compatibility with the MPI mode.
	 */
	bool isExecutor() const
	{
		return false;
	}

	/**
	 * @param size The size of data array in bytes
	 */
	void fillBuffer(unsigned int id, const void* buffer, size_t size)
	{
		assert(id < Base<Executor>::numBuffers());
		assert(m_bufferPos[id]+size <= Base<Executor>::bufferSize(id));

		memcpy(ThreadBase<Executor, Parameter>::_buffer(id)+m_bufferPos[id], buffer, size);
		m_bufferPos[id] += size;
	}
	
	void call(const Parameter &parameters)
	{
		ThreadBase<Executor, Parameter>::call(parameters);

		// Reset the buffer positions
		for (unsigned int i = 0; i < Base<Executor>::numBuffers(); i++)
			m_bufferPos[i] = 0;
	}
};

}

}

#endif // ASYNC_AS_THREAD_H
