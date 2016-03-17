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

#ifdef USE_MPI
#include <mpi.h>
#endif // USE_MPI

#include <cxxtest/TestSuite.h>

#include "async/Dispatcher.h"
#include "async/Module.h"

struct Param
{
};

class Executor : private async::Module<Executor, Param, Param>
{
public:
	bool m_setUp;
	bool m_execInit;
	bool m_exec;
	bool m_tearDown;

public:
	Executor()
		: m_setUp(false),
		  m_execInit(false),
		  m_exec(false),
		  m_tearDown(false)
	{
	}

	bool run()
	{
		async::Dispatcher dispatcher;
#ifdef USE_MPI
		dispatcher.setCommunicator(MPI_COMM_WORLD);
#endif // USE_MPI

		if (dispatcher.init()) {
			// Empty initialization
			setUp();

			Param param;
			callInit(param);

			wait();

			call(param);

			wait();

			finalize();

			return false;
		}

		return true;
	}

	void execInit(const Param &param)
	{
		m_execInit = true;
	}

	void exec(const Param &param)
	{
		m_exec = true;
	}

	void setUp()
	{
		setExecutor(*this);
		m_setUp = true;
	}

	void tearDown()
	{
		m_tearDown = true;
	}
};

class BufferExecutor : private async::Module<BufferExecutor, Param, Param>
{
public:
	unsigned int m_bufferSize;

public:
	BufferExecutor()
		: m_bufferSize(0)
	{
	}

	bool run()
	{
		async::Dispatcher dispatcher;
#ifdef USE_MPI
		dispatcher.setCommunicator(MPI_COMM_WORLD);
#endif // USE_MPI

		if (dispatcher.init()) {
			// Empty initialization
			setUp(42);

			wait();

			Param param;
			call(param);

			wait();

			finalize();

			return false;
		}

		return true;
	}

	void execInit(const Param &param)
	{
	}

	void exec(const Param &param)
	{
		m_bufferSize = bufferSize(0);
	}

	void setUp()
	{
		setUp(0);
	}

	void setUp(unsigned int bufferSize)
	{
		addBuffer(bufferSize);
		setExecutor(*this);
	}

	void tearDown()
	{
	}
};

/**
 * Test for {@link Dispatcher} and {@link Module} since
 * they only work together.
 */
class TestModule : public CxxTest::TestSuite
{
	int m_rank;
	int m_size;

public:
	void setUp()
	{
#ifdef USE_MPI
		MPI_Comm_rank(MPI_COMM_WORLD, &m_rank);
		MPI_Comm_size(MPI_COMM_WORLD, &m_size);
#endif // USE_MPI
	}

	void testModuleDispatcher()
	{
		Executor executor;
		bool isExecutor = executor.run();
		if (isExecutor) {
#ifdef USE_ASYNC_MPI
			TS_ASSERT_EQUALS(m_rank+1, m_size);
#else // USE_ASYNC_MPI
			TS_FAIL("No executors in SYNC and THEAD mode!");
#endif // USE_ASYNC_MPI
		}

		TS_ASSERT(executor.m_setUp);
#ifdef USE_ASYNC_MPI
		if (isExecutor) {
#endif // USE_ASYNC_MPI
			TS_ASSERT(executor.m_execInit);
			TS_ASSERT(executor.m_exec);
			TS_ASSERT(executor.m_tearDown);
#ifdef USE_ASYNC_MPI
		}
#endif // USE_ASYNC_MPI
	}

	void testBuffer()
	{
		BufferExecutor executor;
		bool isExecutor = executor.run();
		if (isExecutor) {
#ifdef USE_ASYNC_MPI
			TS_ASSERT_EQUALS(m_rank+1, m_size);
#else // USE_ASYNC_MPI
			TS_FAIL("No executors in SYNC and THEAD mode!");
#endif // USE_ASYNC_MPI
		}

		unsigned int bufferSize = 42;
#ifdef USE_ASYNC_MPI
		if (isExecutor) {
			bufferSize *= m_size-1;
#endif // USE_ASYNC_MPI
			TS_ASSERT_EQUALS(executor.m_bufferSize, bufferSize);
#ifdef USE_ASYNC_MPI
		}
#endif // USE_ASYNC_MPI
	}
};
