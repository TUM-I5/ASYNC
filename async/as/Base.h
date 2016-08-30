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

#ifndef ASYNC_AS_BASE_H
#define ASYNC_AS_BASE_H

#include <cassert>
#include <cstdlib>
#include <stdint.h>
#include <vector>

#include "utils/logger.h"

#include "async/Config.h"

namespace async
{

namespace as
{

class MPIScheduler;

/**
 * Base class for (a)synchronous communication
 */
template<class Executor, typename InitParameter, typename Parameter>
class Base
{
private:
	/**
	 * Description of a buffer
	 */
	struct Buffer
	{
		/** The original memory */
		const void* origin;
		/** The buffer on the executor (might be NULL) */
		void* buffer;
		/** Size of the original memory */
		size_t size;
	};

private:
	/** The executor for the asynchronous call */
	Executor* m_executor;

	/** The buffers */
	std::vector<Buffer> m_buffer;

	/** Already cleanup everything? */
	bool m_finalized;

	/** Aligment of buffers (might be requested for I/O back-ends) */
	const size_t m_alignment;

protected:
	Base()
		: m_executor(0L),
		  m_finalized(false),
		  m_alignment(async::Config::alignment())
	{
	}

public:
	virtual ~Base()
	{
		_finalize();
	}

	/**
	 * Only required in asynchronous MPI mode
	 */
	virtual void setScheduler(MPIScheduler &scheduler)
	{ }

	virtual void setExecutor(Executor &executor)
	{
		m_executor = &executor;
	}

	/**
	 * @return True, if this is an MPI executor
	 */
	virtual bool isExecutor() const
	{
		return false; // Default for sync and thread
	}

	/**
	 * @param buffer The original memory location in the application
	 * @param bufferSize The size of the memory location
	 * @return The id of the buffer
	 */
	virtual unsigned int addBuffer(const void* buffer, size_t size) = 0;

	unsigned int numBuffers() const
	{
		return m_buffer.size();
	}

	size_t bufferSize(unsigned int id) const
	{
		assert(id < numBuffers());
		return m_buffer[id].size;
	}

	virtual const void* buffer(unsigned int id) const = 0;

	/**
	 * @param size The size that should be transfered
	 */
	virtual void sendBuffer(unsigned int id, size_t size) = 0;

	virtual void callInit(const InitParameter &parameters)
	{
		m_executor->execInit(parameters);
	}

	virtual void call(const Parameter &parameters) = 0;

	virtual void wait() = 0;

	virtual void finalize()
	{
		_finalize();
	}

protected:
	Executor& executor() {
		return *m_executor;
	}

	unsigned int _addBuffer(const void* origin, size_t size, bool allocate = true)
	{
		Buffer buffer;
		buffer.origin = origin;
		buffer.size = size;

		if (size && allocate) {
			if (m_alignment > 0) {
				// Make the allocated buffer size a multiple of m_alignment
				size_t allocBufferSize = (size + m_alignment - 1) / m_alignment;
				allocBufferSize *= m_alignment;

				int ret = posix_memalign(&buffer.buffer, m_alignment, allocBufferSize);
				if (ret)
					logError() << "Could not allocate buffer" << ret;
			} else {
				buffer.buffer = malloc(size);
			}
		} else
			buffer.buffer = 0L;

		m_buffer.push_back(buffer);

		return m_buffer.size()-1;
	}

	/**
	 * Return u_int8_t to allow arithmetic on the pointer
	 */
	const uint8_t* origin(unsigned int id) const
	{
		assert(id < numBuffers());
		return static_cast<const uint8_t*>(m_buffer[id].origin);
	}

	const void* _buffer(unsigned int id) const
	{
		assert(id < numBuffers());
		return m_buffer[id].buffer;
	}

	/**
	 * Return u_int8_t to allow arithmetic on the pointer
	 */
	uint8_t* _buffer(unsigned int id)
	{
		assert(id < numBuffers());
		return static_cast<uint8_t*>(m_buffer[id].buffer);
	}

	/**
	 * Finalize (cleanup) the async call
	 *
	 * @return False if the class was already finalized
	 */
	bool _finalize()
	{
		if (m_finalized)
			return false;

		for (unsigned int i = 0; i < m_buffer.size(); i++) {
			m_buffer[i].origin = 0L;
			free(m_buffer[i].buffer);
			m_buffer[i].buffer = 0L;
			m_buffer[i].size = 0;
		}

		m_finalized = true;
		return true;
	}
};

}

}

#endif // ASYNC_AS_BASE_H
