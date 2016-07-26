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

#include <cstdlib>
#include <stdint.h>
#include <vector>

#include "utils/env.h"
#include "utils/logger.h"

namespace async
{

namespace as
{

/**
 * Base class for (a)synchronous communication
 */
template<class Executor>
class Base
{
private:
	/** The executor for the asynchronous call */
	Executor* m_executor;

	/** The buffers */
	std::vector<void*> m_buffer;

	/** The size of the buffer */
	std::vector<size_t> m_bufferSize;

	/** Already cleanup everything? */
	bool m_finalized;

	/** Aligment of buffers (might be requested for I/O back-ends) */
	size_t m_alignment;

protected:
	Base()
		: m_executor(0L),
		  m_finalized(false)
	{
		m_alignment = utils::Env::get<size_t>("ASYNC_BUFFER_ALIGNMENT", 0);
	}

	~Base()
	{
		for (unsigned int i = 0; i < m_buffer.size(); i++)
			free(m_buffer[i]);
	}

	Executor& executor() {
		return *m_executor;
	}

	/**
	 * Return u_int8_t to allow arithmetic on the pointer
	 */
	u_int8_t* _buffer(unsigned int id)
	{
		return static_cast<uint8_t*>(m_buffer[id]);
	}

public:
	void setExecutor(Executor &executor)
	{
		m_executor = &executor;
	}

	/**
	 * @param bufferSize
	 * @return The id of the buffer
	 */
	unsigned int addBuffer(size_t bufferSize)
	{
		if (bufferSize) {
			void* buffer;
			if (m_alignment > 0) {
				// Make the allocated buffer size a multiple of m_alignment
				size_t allocBufferSize = (bufferSize + m_alignment - 1) / m_alignment;
				allocBufferSize *= m_alignment;

				int ret = posix_memalign(&buffer, m_alignment, allocBufferSize);
				if (ret)
					logError() << "Could not allocate buffer" << ret;
			} else {
				buffer = malloc(bufferSize);
			}
			m_buffer.push_back(buffer);
		} else
			m_buffer.push_back(0L);
		m_bufferSize.push_back(bufferSize);

		return m_buffer.size()-1;
	}

	unsigned int numBuffers() const
	{
		return m_buffer.size();
	}

	const void* buffer(unsigned int id) const
	{
		return m_buffer[id];
	}

	size_t bufferSize(unsigned int id) const
	{
		return m_bufferSize[id];
	}

	/**
	 * Finalize (cleanup) the async call
	 *
	 * @return False if the class was already finalized
	 */
	bool finalize()
	{
		bool finalized = m_finalized;
		m_finalized = true;
		return !finalized;
	}
};

}

}

#endif // ASYNC_AS_BASE_H
