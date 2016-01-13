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
	}

	void setValue(int value)
	{
		m_values.push_back(value);

		if (m_async) {
			size_t size = m_async->bufferSize() / sizeof(int);
			const int* buf = reinterpret_cast<const int*>(m_async->buffer());

			for (size_t i = 0; i < size; i++)
				m_buffers.push_back(buf[i]);
		}
	}

	void testIsExecutor()
	{
		async::AsyncMPI<Executor<TestAsyncMPI>, Parameter, Parameter> async;
		async.setCommunicator(MPI_COMM_WORLD, 3);

		switch (m_rank) {
		case 2:
		case 4:
			TS_ASSERT(async.isExecutor());
			break;
		default:
			TS_ASSERT(!async.isExecutor());
		}
	}

	void testInit()
	{
		Executor<TestAsyncMPI> executor(this);

		async::AsyncMPI<Executor<TestAsyncMPI>, Parameter, Parameter> async;
		async.setCommunicator(MPI_COMM_WORLD, 3);

		async.init(executor, 0);

		if (async.isExecutor())
			async.executorLoop();
		else
			async.wait();
	}

	void testInitCall()
	{
		Executor<TestAsyncMPI> executor(this);

		async::AsyncMPI<Executor<TestAsyncMPI>, Parameter, Parameter> async;
		async.setCommunicator(MPI_COMM_WORLD, 3);

		async.init(executor, 0);

		if (async.isExecutor()) {
			async.executorLoop();

			TS_ASSERT_EQUALS(m_values.size(), 1);
			TS_ASSERT_EQUALS(m_values[0], 42);
		} else {
			async.wait();

			Parameter parameter;
			parameter.value = 42;
			async.initCall(parameter);

			async.wait();
		}
	}

	void testCall()
	{
		Executor<TestAsyncMPI> executor(this);

		async::AsyncMPI<Executor<TestAsyncMPI>, Parameter, Parameter> async;
		async.setCommunicator(MPI_COMM_WORLD, 3);

		async.init(executor, 0);

		if (async.isExecutor()) {
			async.executorLoop();

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

		async::AsyncMPI<Executor<TestAsyncMPI>, Parameter, Parameter> async;
		async.setCommunicator(MPI_COMM_WORLD, 3);


		async.init(executor, sizeof(int));

		if (async.isExecutor()) {
			async.executorLoop();

			for (std::vector<int>::const_iterator i = m_buffers.begin();
					i != m_buffers.end(); i++)
				TS_ASSERT_EQUALS(*i, 43);
		} else {
			async.wait();

			int buffer = 43;
			async.fillBuffer(&buffer, sizeof(int));

			Parameter parameter;
			async.call(parameter);

			async.wait();
		}
	}
};
