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
#include <sched.h>
#include <sys/sysinfo.h>

#include "async/Config.h"
#include "ThreadBase.h"

namespace async
{

namespace as
{

/**
 * Asynchronous call via pthreads
 */
template<class Executor, typename InitParameter, typename Parameter>
class Thread : public ThreadBase<Executor, InitParameter, Parameter>
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

	void setExecutor(Executor &executor)
	{
		ThreadBase<Executor, InitParameter, Parameter>::setExecutor(executor);

		const int numCores = get_nprocs();

		int core = async::Config::getPinCore();
		if (core < 0)
			core = numCores + core;

		if (core < 0 || core >= numCores) {
			logWarning() << "Skipping async thread pining, invalid core id" << core << "specified";
			return;
		}

		cpu_set_t cpuMask;
		CPU_ZERO(&cpuMask);
		CPU_SET(core, &cpuMask);
		ThreadBase<Executor, InitParameter, Parameter>::setAffinity(cpuMask);
	}

	unsigned int addBuffer(const void* buffer, size_t size)
	{
		unsigned int id = ThreadBase<Executor, InitParameter, Parameter>::addBuffer(buffer, size);
		m_bufferPos.push_back(0);

		assert(m_bufferPos.size() == (Base<Executor, InitParameter, Parameter>::numBuffers()));

		return id;
	}

	void sendBuffer(unsigned int id, size_t size)
	{
		assert(id < (Base<Executor, InitParameter, Parameter>::numBuffers()));
		assert(m_bufferPos[id]+size <= (Base<Executor, InitParameter, Parameter>::bufferSize(id)));

		memcpy(Base<Executor, InitParameter, Parameter>::_buffer(id)+m_bufferPos[id],
		       Base<Executor, InitParameter, Parameter>::origin(id)+m_bufferPos[id],
		       size);
		m_bufferPos[id] += size;
	}

	void wait()
	{
		ThreadBase<Executor, InitParameter, Parameter>::wait();

		resetBufferPosition();
	}

	void callInit(const InitParameter &parameters)
	{
		Base<Executor, InitParameter, Parameter>::callInit(parameters);

		resetBufferPosition();
	}

	void call(const Parameter &parameters)
	{
		ThreadBase<Executor, InitParameter, Parameter>::call(parameters);
	}

private:
	void resetBufferPosition()
	{
		for (std::vector<size_t>::iterator it = m_bufferPos.begin();
			it != m_bufferPos.end(); it++)
			*it = 0;
	}
};

}

}

#endif // ASYNC_AS_THREAD_H
