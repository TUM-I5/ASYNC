// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#include <cxxtest/TestSuite.h>

#include "Executor.h"
#include "async/as/Sync.h"

class Param;
class TestSync : public CxxTest::TestSuite {
  private:
  int m_value{};

  public:
  void setValue(int value) { m_value = value; }

  void testInit() {
    Executor<TestSync> executor(this);

    async::as::Sync<Executor<TestSync>, Parameter, Parameter> async;
    async.setExecutor(executor);

    async.wait();
  }

  void testCall() {
    Executor<TestSync> executor(this);

    async::as::Sync<Executor<TestSync>, Parameter, Parameter> async;
    async.setExecutor(executor);

    TS_ASSERT_EQUALS(async.numBuffers(), 0);

    m_value = 0;

    async.wait();
    Parameter parameter;
    parameter.value = 42;
    async.call(parameter);

    async.wait(); // Make sure the call is finished
    TS_ASSERT_EQUALS(m_value, 42);

    parameter.value = 415;
    async.call(parameter);

    async.wait();
    TS_ASSERT_EQUALS(m_value, 415);
  }

  void testBuffer() {
    Executor<TestSync> executor(this);

    async::as::Sync<Executor<TestSync>, Parameter, Parameter> async;
    async.setExecutor(executor);

    int buffer = 2;
    TS_ASSERT_EQUALS(async.addBuffer(&buffer, sizeof(int)), 0);
    TS_ASSERT_EQUALS(async.numBuffers(), 1);

    TS_ASSERT_EQUALS(&buffer, async.buffer(0));

    async.wait();
  }

  void testResizeBuffer() {
    Executor<TestSync> executor(this);

    async::as::Sync<Executor<TestSync>, Parameter, Parameter> async;
    async.setExecutor(executor);

    int buffer1 = 1;
    TS_ASSERT_EQUALS(async.addBuffer(&buffer1, sizeof(int)), 0);
    TS_ASSERT_EQUALS(async.numBuffers(), 1);

    TS_ASSERT_EQUALS(async.bufferSize(0), sizeof(int));

    async.wait();

    const auto buffer2 = std::array<int, 2>{2, 3};

    async.resizeBuffer(0, buffer2.data(), 2 * sizeof(int));
    TS_ASSERT_EQUALS(async.bufferSize(0), 2 * sizeof(int));
    TS_ASSERT_EQUALS(*static_cast<const int*>(async.buffer(0)), 2);
  }

  void testRemoveBuffer() {
    Executor<TestSync> executor(this);

    async::as::Sync<Executor<TestSync>, Parameter, Parameter> async;
    async.setExecutor(executor);

    int buffer = 2;
    async.addBuffer(&buffer, sizeof(int));

    async.wait();

    async.removeBuffer(0);
    TS_ASSERT_EQUALS(static_cast<const void*>(nullptr), async.buffer(0));
  }

  void testManagedBuffer() {
    Executor<TestSync> executor(this);

    async::as::Sync<Executor<TestSync>, Parameter, Parameter> async;
    async.setExecutor(executor);

    async.addBuffer(nullptr, sizeof(int));

    *static_cast<int*>(async.managedBuffer(0)) = 2;

    TS_ASSERT_EQUALS(*static_cast<const int*>(async.buffer(0)), 2);

    async.wait();
  }
};
