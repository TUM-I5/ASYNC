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

#ifndef ASYNC_BASE_H
#define ASYNC_BASE_H

#include <cstring>

namespace async
{

/**
 * Allocates a buffer using C++ allocation
 */
class DefaultAllocator
{
public:
	template<typename T>
	static T* alloc(size_t size)
	{
		return new T[size];
	}

	template<typename T>
	static void dealloc(T* mem)
	{
		delete [] mem;
	}
};

/**
 * Base class for (a)synchronous communication
 */
template<class Executor, class Allocator>
class Base
{
private:
	/** The executor for the asynchronous call */
	Executor* m_executor;

	/** The buffer */
	char* m_buffer;

	/** The size of the buffer */
	size_t m_bufferSize;

	/** Already cleanup everything? */
	bool m_finalized;

protected:
	Base()
		: m_executor(0L),
		  m_buffer(0L), m_bufferSize(0),
		  m_finalized(false)
	{ }

	~Base()
	{
		Allocator::dealloc(m_buffer);
	}

	void init(Executor &executor, size_t bufferSize)
	{
		m_executor = &executor;
		m_buffer = Allocator::template alloc<char>(bufferSize);
		m_bufferSize = bufferSize;
	}

	Executor& executor() {
		return *m_executor;
	}

	char* _buffer()
	{
		return m_buffer;
	}

	/**
	 * Finalize (cleanup) the async call
	 *
	 * @return False if the class was already finalized
	 */
	bool finalize()
	{
		bool finalized = m_finalized;
		m_finalized = false;
		return !finalized;
	}

public:
	const void* buffer() const
	{
		return m_buffer;
	}

	size_t bufferSize() const
	{
		return m_bufferSize;
	}
};

}

#endif // ASYNC_BASE_H
