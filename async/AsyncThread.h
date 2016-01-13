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

#include <pthread.h>
#include <sched.h>

#include "utils/logger.h"

#include "async/Base.h"

namespace async
{

/**
 * Asynchronous call via pthreads
 */
template<class Executor, typename Parameter>
class AsyncThread : public Base<Executor, DefaultAllocator>
{
private:
	/** Async thread */
	pthread_t m_asyncThread;
	
	/** Mutex locked by the writer (caller) */
	pthread_spinlock_t m_writerLock;
	
	/** Mutex locked by the reader (callee) */
	pthread_mutex_t m_readerLock;

	/** The current buffer position */
	size_t m_bufferPos;

	/** Parameters for the next call */
	Parameter m_nextParams;
	
	/** Shutdown the thread */
	bool m_shutdown;

public:
	AsyncThread()
		: m_asyncThread(pthread_self()),
		  m_bufferPos(0),
		  m_shutdown(false)
	{
		pthread_spin_init(&m_writerLock, PTHREAD_PROCESS_PRIVATE);
		pthread_mutex_init(&m_readerLock, 0L);
	}
	
	~AsyncThread()
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
		Base<Executor, DefaultAllocator>::init(executor, bufferSize);
		
		// Lock the reader until data is available
		pthread_mutex_lock(&m_readerLock);

		if (pthread_create(&m_asyncThread, 0L, asyncThread, this) != 0)
			logError() << "Failed to start async thread";
	}
	
	void setAffinity(const cpu_set_t &cpuSet)
	{
		pthread_setaffinity_np(m_asyncThread, sizeof(cpu_set_t), &cpuSet);
	}

	/**
	 * Wait for the asynchronous call to finish
	 */
	void wait()
	{
		pthread_spin_lock(&m_writerLock);
	}

	void fillBuffer(const void* buffer, size_t size)
	{
		memcpy(Base<Executor, DefaultAllocator>::_buffer()+m_bufferPos, buffer, size);
		m_bufferPos += size;
	}
	
	void call(const Parameter &parameters)
	{

		memcpy(&m_nextParams, &parameters, sizeof(Parameter));
		
		pthread_mutex_unlock(&m_readerLock);

		// Reset the buffer position
		m_bufferPos = 0;
	}

	void finalize()
	{
		if (!Base<Executor, DefaultAllocator>::finalize())
			return;

		// Shutdown the thread
		m_shutdown = true;
		pthread_mutex_unlock(&m_readerLock);
		pthread_join(m_asyncThread, 0L);
	}
	
private:
	static void* asyncThread(void* c)
	{
		AsyncThread* async = reinterpret_cast<AsyncThread*>(c);

		while (true) {
			// We assume that this lock happens before any unlock from the main thread
			pthread_mutex_lock(&async->m_readerLock);
			if (async->m_shutdown)
				break;
			
			async->executor().exec(async->m_nextParams);
			
			pthread_spin_unlock(&async->m_writerLock);
		}

		return 0L;
	}
};

}

#endif // ASYNC_ASYNCTHREAD_H
