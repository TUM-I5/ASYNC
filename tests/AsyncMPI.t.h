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

#include <mpi.h>

#include <vector>

#include <cxxtest/TestSuite.h>

#include "async/AsyncMPI.h"
#include "Executor.h"

class TestAsyncMPI : public CxxTest::TestSuite
{
	async::AsyncMPIScheduler* m_scheduler;

	int m_rank;

	std::vector<int> m_values;

	async::AsyncMPI<Executor<TestAsyncMPI>, Parameter, Parameter>* m_async;
	std::vector<int> m_buffers;

public:
	void setUp()
	{
		MPI_Comm_rank(MPI_COMM_WORLD, &m_rank);
		m_values.clear();

		m_async = 0L;
		m_buffers.clear();

		m_scheduler = new async::AsyncMPIScheduler();
		m_scheduler->setCommunicator(MPI_COMM_WORLD, 3);
	}

	void tearDown()
	{
		//MPI_Barrier(MPI_COMM_WORLD);
		delete m_scheduler;
	}

	void setValue(int value)
	{
		m_values.push_back(value);

		if (m_async) {
			for (unsigned int i = 0; i < m_async->numBuffers(); i++) {
				size_t size = m_async->bufferSize(i) / sizeof(int);
				const int* buf = reinterpret_cast<const int*>(m_async->buffer(i));

				for (size_t j = 0; j < size; j++)
					m_buffers.push_back(buf[j]);
			}
		}
	}

	void testInit()
	{
		Executor<TestAsyncMPI> executor(this);

		async::AsyncMPI<Executor<TestAsyncMPI>, Parameter, Parameter> async(*m_scheduler);

		async.setExecutor(executor);

		if (m_scheduler->isExecutor())
			m_scheduler->loop();
		else
			async.wait();
	}

	void testInitCall()
	{
		Executor<TestAsyncMPI> executor(this);

		async::AsyncMPI<Executor<TestAsyncMPI>, Parameter, Parameter> async(*m_scheduler);

		async.setExecutor(executor);

		if (m_scheduler->isExecutor()) {
			m_scheduler->loop();

			TS_ASSERT_EQUALS(m_values.size(), 1);
			TS_ASSERT_EQUALS(m_values[0], 42);
		} else {
			Parameter parameter;
			parameter.value = 42;
			async.callInit(parameter);

			async.wait();
		}
	}

	void testCall()
	{
		Executor<TestAsyncMPI> executor(this);

		async::AsyncMPI<Executor<TestAsyncMPI>, Parameter, Parameter> async(*m_scheduler);

		async.setExecutor(executor);

		TS_ASSERT_EQUALS(async.numBuffers(), 0);

		if (m_scheduler->isExecutor()) {
			m_scheduler->loop();

			TS_ASSERT_EQUALS(m_values.size(), 3);
			TS_ASSERT_EQUALS(m_values[0], 1);
			TS_ASSERT_EQUALS(m_values[1], 42);
			TS_ASSERT_EQUALS(m_values[2], 415);
		} else {
			async.wait();
			Parameter parameter;
			parameter.value = 1;
			async.call(parameter);

			async.wait();
			parameter.value = 42;
			async.call(parameter);

			async.wait(); // Make sure the call is finished

			parameter.value = 415;
			async.call(parameter);

			async.wait();
		}
	}

	void testBuffer()
	{
		Executor<TestAsyncMPI> executor(this);

		async::AsyncMPI<Executor<TestAsyncMPI>, Parameter, Parameter> async(*m_scheduler);

		async.addBuffer(m_scheduler->isExecutor() ? 0 : sizeof(int));

		async.setExecutor(executor);
		m_async = &async;

		TS_ASSERT_EQUALS(async.numBuffers(), 1);

		if (m_scheduler->isExecutor()) {
			m_scheduler->loop();

			for (std::vector<int>::const_iterator i = m_buffers.begin();
					i != m_buffers.end(); i++)
				TS_ASSERT_EQUALS(*i, 43);
		} else {
			async.wait();

			int buffer = 43;
			async.fillBuffer(0, &buffer, sizeof(int));

			Parameter parameter;
			async.call(parameter);

			async.wait();
		}
	}

	void testBuffer2()
	{
		Executor<TestAsyncMPI> executor(this);

		async::AsyncMPI<Executor<TestAsyncMPI>, Parameter, Parameter> async(*m_scheduler);

		async.addBuffer(m_scheduler->isExecutor() ? 0 : sizeof(int));
		async.addBuffer(m_scheduler->isExecutor() ? 0 : sizeof(int));

		async.setExecutor(executor);
		m_async = &async;

		TS_ASSERT_EQUALS(async.numBuffers(), 2);

		if (m_scheduler->isExecutor()) {
			m_scheduler->loop();

			TS_ASSERT_EQUALS(m_buffers.size() % 2, 0);

			for (unsigned int i = 0; i < m_buffers.size()/2; i++)
				TS_ASSERT_EQUALS(m_buffers[i], 43);
			for (unsigned int i = m_buffers.size()/2; i < m_buffers.size(); i++)
				TS_ASSERT_EQUALS(m_buffers[i], 42);
		} else {
			async.wait();

			int buffer = 43;
			async.fillBuffer(0, &buffer, sizeof(int));

			buffer = 42;
			async.fillBuffer(1, &buffer, sizeof(int));

			Parameter parameter;
			async.call(parameter);

			async.wait();
		}
	}

	void testMultiple()
	{
		Executor<TestAsyncMPI> executor(this);

		async::AsyncMPI<Executor<TestAsyncMPI>, Parameter, Parameter> async1(*m_scheduler);
		async1.setExecutor(executor);

		async::AsyncMPI<Executor<TestAsyncMPI>, Parameter, Parameter> async2(*m_scheduler);
		async2.setExecutor(executor);

		if (m_scheduler->isExecutor()) {
			m_scheduler->loop();

			TS_ASSERT_EQUALS(m_values.size(), 2);
			TS_ASSERT_EQUALS(m_values[0]+m_values[1], 43); // We cannot be sure with call arrives first
		} else {
			async1.wait();
			async2.wait();

			Parameter parameter;
			parameter.value = 1;
			async1.call(parameter);

			parameter.value = 42;
			async2.call(parameter);

			async1.wait();
			async2.wait();
		}
	}

	void testLargeBuffer()
	{
		Executor<TestAsyncMPI> executor(this);

		async::AsyncMPI<Executor<TestAsyncMPI>, Parameter, Parameter> async(*m_scheduler);

		size_t bufferSize = (1UL<<30) + (1UL<<29); // 1.5 GB
		char* buffer = 0L;
		if (m_scheduler->isExecutor()) {
			async.addBuffer(0);
		} else {
			buffer = new char[bufferSize];
			async.addBuffer(bufferSize);
		}

		async.setExecutor(executor);

		if (m_scheduler->isExecutor()) {
			m_scheduler->loop();

			// Group size without the communicator
			int groupSize = async.bufferSize(0) / bufferSize;
			TS_ASSERT_LESS_THAN_EQUALS(1, groupSize);
			TS_ASSERT_LESS_THAN_EQUALS(groupSize, 2);

			TS_ASSERT_EQUALS(async.bufferSize(0), bufferSize*groupSize);

			const char* buf = reinterpret_cast<const char*>(async.buffer(0));
			for (int i = 0; i < groupSize; i++) {
				TS_ASSERT_EQUALS(buf[0], 'A');
				TS_ASSERT_EQUALS(buf[bufferSize-1], 'Z');

				buf += bufferSize;
			}
		} else {
			async.wait();

			buffer[0] = 'A';
			buffer[bufferSize-1] = 'Z';
			async.fillBuffer(0, buffer, bufferSize);

			Parameter parameter;
			async.call(parameter);

			async.wait();
		}

		delete [] buffer;
	}
};
