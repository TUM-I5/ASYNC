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

#include <cxxtest/TestSuite.h>

#include "async/AsyncThread.h"
#include "Executor.h"

class TestAsyncThread : public CxxTest::TestSuite
{
private:
	pthread_spinlock_t m_lock;

	int m_value;

public:
	void setValue(int value)
	{
		// Lock the variable to test multiple threads at once
		pthread_spin_lock(&m_lock);
		m_value += value;
		pthread_spin_unlock(&m_lock);
	}

	void setUp()
	{
		pthread_spin_init(&m_lock, PTHREAD_PROCESS_PRIVATE);
		m_value = 0;
	}

	void testInit()
	{
		Executor<TestAsyncThread> executor(this);

		async::AsyncThread<Executor<TestAsyncThread>, Parameter> async;
		async.setExecutor(executor);

		async.wait();
	}

	void testCall()
	{
		Executor<TestAsyncThread> executor(this);

		async::AsyncThread<Executor<TestAsyncThread>, Parameter> async;
		async.setExecutor(executor);

		TS_ASSERT_EQUALS(async.numBuffers(), 0);

		async.wait();
		Parameter parameter;
		parameter.value = 42;
		async.call(parameter);

		async.wait(); // Make sure the call is finished
		TS_ASSERT_EQUALS(m_value, 42);

		parameter.value = 415;
		async.call(parameter);

		async.wait();
		TS_ASSERT_EQUALS(m_value, 42+415);
	}

	void testBuffer()
	{
		Executor<TestAsyncThread> executor(this);

		async::AsyncThread<Executor<TestAsyncThread>, Parameter> async;
		async.addBuffer(sizeof(int));
		async.setExecutor(executor);

		int buffer = 42;

		async.wait();
		async.fillBuffer(0, &buffer, sizeof(int));
		TS_ASSERT_EQUALS(*reinterpret_cast<const int*>(async.buffer(0)), 42);
	}

	void testBuffer2()
	{
		Executor<TestAsyncThread> executor(this);

		async::AsyncThread<Executor<TestAsyncThread>, Parameter> async;
		async.addBuffer(sizeof(int));
		async.addBuffer(sizeof(int));
		async.setExecutor(executor);

		int buffer = 42;

		async.wait();
		async.fillBuffer(0, &buffer, sizeof(int));
		TS_ASSERT_EQUALS(*reinterpret_cast<const int*>(async.buffer(0)), 42);

		buffer = 12;

		async.fillBuffer(1, &buffer, sizeof(int));
		TS_ASSERT_EQUALS(*reinterpret_cast<const int*>(async.buffer(1)), 12);
	}

	void testMultiple()
	{
		Executor<TestAsyncThread> executor(this);

		async::AsyncThread<Executor<TestAsyncThread>, Parameter> async1;
		async1.setExecutor(executor);

		async::AsyncThread<Executor<TestAsyncThread>, Parameter> async2;
		async2.setExecutor(executor);

		async1.wait();
		async2.wait();

		Parameter parameter;
		parameter.value = 42;
		async1.call(parameter);

		parameter.value = 13;
		async2.call(parameter);

		async1.wait();
		async2.wait();

		TS_ASSERT_EQUALS(m_value, 42+13);
	}
};
