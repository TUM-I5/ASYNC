// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#include <array>
#include <cstdint>
#include <cxxtest/TestSuite.h>

#include <cmath>

#include "Executor.h"
#include "async/as/Thread.h"

class TestThread : public CxxTest::TestSuite {
  private:
  pthread_spinlock_t m_lock{};

  int m_value{};

  public:
  void setValue(int value) { m_value += value; }

  /**
   * Tests the buffer alignement.
   * Make sure that the environment variable ASYNC_BUFFER_ALIGNMENT=65536
   * is set for this test.
   */
  void testBuffer() {
    Executor<TestThread> executor(this);

    async::as::Thread<Executor<TestThread>, Parameter, Parameter> async;
    async.setExecutor(executor);

    int buffer = 42;
    async.addBuffer(&buffer, sizeof(int));

    async.wait();
    async.sendBuffer(0, sizeof(int));
    TS_ASSERT_EQUALS(*reinterpret_cast<const int*>(async.buffer(0)), 42);
    const auto p = reinterpret_cast<uintptr_t>(async.buffer(0));
    TS_ASSERT_EQUALS(p % 65536, 0);
  }

  /**
   * Tests that a resized buffer is aligned as well (with ASYNC_BUFFER_ALIGNMENT=65536).
   */
  void testResizedBuffer() {
    Executor<TestThread> executor(this);

    async::as::Thread<Executor<TestThread>, Parameter, Parameter> async;
    async.setExecutor(executor);

    const std::array<int, 3> values{42, 43, 44};
    async.addBuffer(values.data(), sizeof(int));
    async.sendBuffer(0, sizeof(int));

    async.wait();
    async.resizeBuffer(0, values.data(), values.size() * sizeof(int));
    async.sendBuffer(0, values.size() * sizeof(int));

    const auto* resized = reinterpret_cast<const int*>(async.buffer(0));
    TS_ASSERT_EQUALS(resized[2], 44);
    TS_ASSERT_EQUALS(reinterpret_cast<uintptr_t>(resized) % 65536, 0);
  }
};
