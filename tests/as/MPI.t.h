// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#include <cstddef>
#include "async/Config.h"
#include "async/as/MPIScheduler.h"
#include <array>
#include <mpi.h>

#include <pthread.h>
#include <vector>

#include <cxxtest/GlobalFixture.h>
#include <cxxtest/TestSuite.h>

#ifdef ASYNC_MPI_COPY
#include "async/as/MPIAsync.h"
#else // ASYNC_MPI_COPY
#include "async/as/MPI.h"
#endif // ASYNC_MPI_COPY
#include "Executor.h"

#ifdef ASYNC_MPI_COPY
template <class Executor, typename InitParameter, typename Parameter>
struct MPITest {
  using Type = async::as::MPIAsync<Executor, InitParameter, Parameter>;
};
#else  // ASYNC_MPI_COPY
template <class Executor, typename InitParameter, typename Parameter>
struct MPITest {
  using Type = async::as::MPI<Executor, InitParameter, Parameter>;
};
#endif // ASYNC_MPI_COPY

/**
 * Setup for large buffer testing
 */
class LargeBuffer : public CxxTest::GlobalFixture {
  size_t m_size{};

  public:
  auto setUpWorld() -> bool override {
    m_size = (3 * async::Config::maxSend()) / 2 + 2;
    return true;
  }

  [[nodiscard]] auto size() const -> size_t { return m_size; }
};

static LargeBuffer largeBuffer;

class TestMPI : public CxxTest::TestSuite {
  async::as::MPIScheduler* m_scheduler{};

  int m_rank{};

  std::vector<int> m_values;
  pthread_spinlock_t m_valueLock{};

  MPITest<Executor<TestMPI>, Parameter, Parameter>::Type* m_async{};
  std::vector<int> m_buffers;

  bool m_largeBufferTest{};

  public:
  void setUp() override {
    MPI_Comm_rank(MPI_COMM_WORLD, &m_rank);

    m_values.clear();
    pthread_spin_init(&m_valueLock, PTHREAD_PROCESS_PRIVATE);

    m_async = nullptr;
    m_buffers.clear();
    m_largeBufferTest = false;

    m_scheduler = new async::as::MPIScheduler();
    m_scheduler->setCommunicator(MPI_COMM_WORLD, 2);
  }

  void tearDown() override {
    // MPI_Barrier(MPI_COMM_WORLD);
    delete m_scheduler;

    pthread_spin_destroy(&m_valueLock);
  }

  void setValue(int value) {
    pthread_spin_lock(&m_valueLock);
    m_values.push_back(value);
    pthread_spin_unlock(&m_valueLock);

    if (m_largeBufferTest) {
      // Group size without the communicator
      const int groupSize = static_cast<int>(m_async->bufferSize(0) / largeBuffer.size());
      TS_ASSERT_LESS_THAN_EQUALS(1, groupSize);
      TS_ASSERT_LESS_THAN_EQUALS(groupSize, 2);

      TS_ASSERT_EQUALS(m_async->bufferSize(0), largeBuffer.size() * groupSize);

      const char* buf = reinterpret_cast<const char*>(m_async->buffer(0));
      for (int i = 0; i < groupSize; i++) {
        TS_ASSERT_EQUALS(buf[0], 'A');
        TS_ASSERT_EQUALS(buf[largeBuffer.size() - 1], 'Z');

        buf += largeBuffer.size();
      }
    } else if (m_async != nullptr) {
      for (unsigned int i = 0; i < m_async->numBuffers(); i++) {
        const size_t size = m_async->bufferSize(i) / sizeof(int);
        const int* buf = reinterpret_cast<const int*>(m_async->buffer(i));

        for (size_t j = 0; j < size; j++) {
          m_buffers.push_back(buf[j]);
        }
      }
    }
  }

  void testInit() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

    async.setExecutor(executor);

    if (m_scheduler->isExecutor()) {
      m_scheduler->loop();
    } else {
      async.wait();
    }
  }

  void testInitCall() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

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

  void testCall() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

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

  void testBuffer() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

    async.setExecutor(executor);
    m_async = &async;

    if (m_scheduler->isExecutor()) {
      m_scheduler->loop();

      for (const auto& i : m_buffers) {
        TS_ASSERT_EQUALS(i, 43);
      }
    } else {
      int buffer = 43;
      TS_ASSERT_EQUALS(async.addBuffer(&buffer, sizeof(int)), 0);

      TS_ASSERT_EQUALS(async.numBuffers(), 1);

      TS_ASSERT_EQUALS(async.managedBuffer(0), static_cast<void*>(nullptr));

      async.wait();

      async.sendBuffer(0, sizeof(int));

      const Parameter parameter;
      async.call(parameter);

      async.wait();
    }
  }

  void testEmptyBuffer() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

    async.setExecutor(executor);
    m_async = &async;

    if (m_scheduler->isExecutor()) {
      m_scheduler->loop();
    } else {
      int buffer = 0;
      async.addBuffer(&buffer, sizeof(int));

      async.wait();

      const Parameter parameter;
      async.call(parameter); // Should not timeout ...

      async.wait();
    }
  }

  /**
   * Test transmitting buffer before initialization
   */
  void testInitBuffer() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

    async.setExecutor(executor);
    m_async = &async;

    if (m_scheduler->isExecutor()) {
      m_scheduler->loop();

      for (const auto& i : m_buffers) {
        TS_ASSERT_EQUALS(i, 43);
      }
    } else {
      int buffer = 43;
      async.addBuffer(&buffer, sizeof(int));

      async.sendBuffer(0, sizeof(int));

      const Parameter parameter;
      async.callInit(parameter);

      async.wait();
    }
  }

  void testSyncBuffer() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

    async.setExecutor(executor);
    m_async = &async;

    if (m_scheduler->isExecutor()) {
      m_scheduler->loop();

      TS_ASSERT_LESS_THAN_EQUALS(1, m_buffers.size());
      TS_ASSERT_LESS_THAN_EQUALS(m_buffers.size(), 2);
      for (const auto& i : m_buffers) {
        TS_ASSERT_EQUALS(i, 3);
      }
    } else {
      int buffer = 3;
      async.addSyncBuffer(&buffer, sizeof(int));

      async.sendBuffer(0, sizeof(int));

      const Parameter parameter;
      async.callInit(parameter);

      async.wait();
    }
  }

  void testSyncCloneBuffer() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

    async.setExecutor(executor);
    m_async = &async;

    if (m_scheduler->isExecutor()) {
      m_scheduler->loop();

      TS_ASSERT_EQUALS(m_buffers.size(), 1);
      for (const auto& i : m_buffers) {
        TS_ASSERT_EQUALS(i, 3);
      }
    } else {
      int buffer = 3;
      async.addSyncBuffer(&buffer, sizeof(int), true);

      async.sendBuffer(0, sizeof(int));

      const Parameter parameter;
      async.callInit(parameter);

      async.wait();
    }
  }

  void testCloneBuffer() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

    async.setExecutor(executor);
    m_async = &async;

    if (m_scheduler->isExecutor()) {
      m_scheduler->loop();

      TS_ASSERT_EQUALS(m_buffers.size(), 1);
      for (const auto& i : m_buffers) {
        TS_ASSERT_EQUALS(i, 3);
      }
    } else {
      int buffer = 3;
      async.addBuffer(&buffer, sizeof(int), true);

      async.sendBuffer(0, sizeof(int));

      const Parameter parameter;
      async.callInit(parameter);

      async.wait();
    }
  }

  void testBuffer2() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

    async.setExecutor(executor);
    m_async = &async;

    if (m_scheduler->isExecutor()) {
      m_scheduler->loop();

      TS_ASSERT_EQUALS(m_buffers.size() % 2, 0);

      for (unsigned int i = 0; i < m_buffers.size() / 2; i++)
        TS_ASSERT_EQUALS(m_buffers[i], 43);
      for (unsigned int i = m_buffers.size() / 2; i < m_buffers.size(); i++)
        TS_ASSERT_EQUALS(m_buffers[i], 42);
    } else {
      int buffer0 = 43;
      async.addBuffer(&buffer0, sizeof(int));
      int buffer1 = 42;
      async.addBuffer(&buffer1, sizeof(int));

      TS_ASSERT_EQUALS(async.numBuffers(), 2);

      async.wait();

      async.sendBuffer(0, sizeof(int));

      async.sendBuffer(1, sizeof(int));

      const Parameter parameter;
      async.call(parameter);

      async.wait();
    }
  }

  void testMixedBuffer() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

    async.setExecutor(executor);
    m_async = &async;

    if (m_scheduler->isExecutor()) {
      m_scheduler->loop();

      TS_ASSERT_LESS_THAN_EQUALS(4, m_buffers.size());
      TS_ASSERT_LESS_THAN_EQUALS(m_buffers.size(), 8);
      for (const auto& i : m_buffers) {
        TS_ASSERT_LESS_THAN_EQUALS(3, i);
        TS_ASSERT_LESS_THAN_EQUALS(i, 4);
      }
    } else {
      int buffer0 = 3;
      async.addSyncBuffer(&buffer0, sizeof(int));

      int buffer1 = 4;
      async.addBuffer(&buffer1, sizeof(int));

      async.sendBuffer(0, sizeof(int));
      async.sendBuffer(1, sizeof(int));

      const Parameter parameter;
      async.callInit(parameter);

      async.wait();

      async.sendBuffer(1, sizeof(int));

      async.call(parameter);

      async.wait();
    }
  }

  void testResizeBuffer() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

    async.setExecutor(executor);
    m_async = &async;

    if (m_scheduler->isExecutor()) {
      m_scheduler->loop();

      std::vector<int> buffers;
      if (m_rank == 2) {
        buffers.push_back(3);
        buffers.push_back(3);
        buffers.push_back(4);
        buffers.push_back(5);
        buffers.push_back(6);
        buffers.push_back(4);
        buffers.push_back(5);
        buffers.push_back(6);
        buffers.push_back(9);
        buffers.push_back(9);

        buffers.push_back(4);
        buffers.push_back(5);
        buffers.push_back(4);
        buffers.push_back(5);
        buffers.push_back(4);
        buffers.push_back(5);
        buffers.push_back(6);
        buffers.push_back(4);
        buffers.push_back(5);
        buffers.push_back(6);
        buffers.push_back(9);
        buffers.push_back(9);

        buffers.push_back(4);
        buffers.push_back(5);
        buffers.push_back(4);
        buffers.push_back(5);
        buffers.push_back(10);
        buffers.push_back(10);
        buffers.push_back(9);
        buffers.push_back(9);

        buffers.push_back(4);
        buffers.push_back(5);
        buffers.push_back(4);
        buffers.push_back(5);
        buffers.push_back(10);
        buffers.push_back(10);
        buffers.push_back(20);
        buffers.push_back(21);
        buffers.push_back(20);
        buffers.push_back(21);
      } else {
        buffers.push_back(3);
        buffers.push_back(4);
        buffers.push_back(5);
        buffers.push_back(6);
        buffers.push_back(9);

        buffers.push_back(4);
        buffers.push_back(5);
        buffers.push_back(4);
        buffers.push_back(5);
        buffers.push_back(6);
        buffers.push_back(9);

        buffers.push_back(4);
        buffers.push_back(5);
        buffers.push_back(10);
        buffers.push_back(9);

        buffers.push_back(4);
        buffers.push_back(5);
        buffers.push_back(10);
        buffers.push_back(20);
        buffers.push_back(21);
      }

      TS_ASSERT_EQUALS(m_buffers.size(), buffers.size());

      for (size_t i = 0; i < m_buffers.size(); i++) {
        TS_ASSERT_EQUALS(m_buffers[i], buffers[i]);
      }
    } else {
      int buffer0 = 3;
      async.addSyncBuffer(&buffer0, sizeof(int));

      const auto buffer1 = std::array<int, 3>{4, 5, 6};
      async.addBuffer(buffer1.data(), 3 * sizeof(int));

      int buffer2 = 9;
      async.addBuffer(&buffer2, sizeof(int));

      async.sendBuffer(0, sizeof(int));
      async.sendBuffer(1, 3 * sizeof(int));
      async.sendBuffer(2, sizeof(int));

      const Parameter parameter;
      async.callInit(parameter);

      async.wait();

      const auto buffer3 = std::array<int, 2>{4, 5};
      async.resizeBuffer(0, buffer3.data(), 2 * sizeof(int));

      async.sendBuffer(0, 2 * sizeof(int));

      async.call(parameter);

      async.wait();

      int buffer4 = 10;
      async.resizeBuffer(1, &buffer4, sizeof(int));

      async.sendBuffer(1, sizeof(int));

      async.call(parameter);

      async.wait();

      const auto buffer5 = std::array<int, 2>{4, 5};
      async.resizeBuffer(2, buffer5.data(), 2 * sizeof(int));

      async.sendBuffer(2, 2 * sizeof(int));

      async.call(parameter);

      async.wait();
    }
  }

  void testRemoveBuffer() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

    async.setExecutor(executor);
    m_async = &async;

    if (m_scheduler->isExecutor()) {
      m_scheduler->loop();
    } else {
      int buffer0 = 3;
      async.addSyncBuffer(&buffer0, sizeof(int));

      int buffer1 = 4;
      async.addBuffer(&buffer1, sizeof(int));

      async.sendBuffer(0, sizeof(int));
      async.sendBuffer(1, sizeof(int));

      const Parameter parameter;
      async.callInit(parameter);

      async.wait();

      async.removeBuffer(0);
      TS_ASSERT_EQUALS(async.buffer(0), static_cast<const void*>(nullptr));

      async.sendBuffer(1, sizeof(int));

      async.call(parameter);

      async.wait();

      async.removeBuffer(1);
      TS_ASSERT_EQUALS(async.buffer(1), static_cast<const void*>(nullptr));

      async.call(parameter);

      async.wait();
    }
  }

  void testManagedBuffer() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

    async.setExecutor(executor);
    m_async = &async;

    if (m_scheduler->isExecutor()) {
      m_scheduler->loop();

      for (const auto& i : m_buffers) {
        TS_ASSERT_EQUALS(i, 43);
      }
    } else {
      async.addBuffer(nullptr, sizeof(int));

      TS_ASSERT_DIFFERS(async.managedBuffer(0), static_cast<void*>(nullptr));

      async.wait();

      *static_cast<int*>(async.managedBuffer(0)) = 43;
      async.sendBuffer(0, sizeof(int));

      const Parameter parameter;
      async.call(parameter);

      async.wait();
    }
  }

  void testRemoveManagedBuffer() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

    async.setExecutor(executor);
    m_async = &async;

    if (m_scheduler->isExecutor()) {
      m_scheduler->loop();
    } else {
      async.addBuffer(nullptr, 200 * sizeof(int));
      async.addBuffer(nullptr, sizeof(int));

      async.wait();

      async.sendBuffer(0, 200 * sizeof(int));
      async.sendBuffer(1, sizeof(int));

      const Parameter parameter;
      async.call(parameter);

      async.wait();

      async.removeBuffer(1);
      TS_ASSERT_DIFFERS(async.managedBuffer(0), static_cast<const void*>(nullptr));

      static_cast<int*>(async.managedBuffer(0))[199] = 1;
      async.sendBuffer(0, 2 * sizeof(int));

      async.call(parameter);

      async.wait();
    }
  }

  void testMultiple() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async1;
    async1.setScheduler(*m_scheduler);
    async1.setExecutor(executor);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async2;
    async2.setScheduler(*m_scheduler);
    async2.setExecutor(executor);

    if (m_scheduler->isExecutor()) {
      m_scheduler->loop();

      TS_ASSERT_EQUALS(m_values.size(), 2);
      TS_ASSERT_EQUALS(m_values[0] + m_values[1], 43); // We cannot be sure which call arrives first
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

  void testLargeBuffer() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

    async.setExecutor(executor);
    m_async = &async;
    m_largeBufferTest = true;

    if (m_scheduler->isExecutor()) {
      m_scheduler->loop();
    } else {
      char* buffer = new char[largeBuffer.size()];
      async.addBuffer(buffer, largeBuffer.size());

      async.wait();

      buffer[0] = 'A';
      buffer[largeBuffer.size() - 1] = 'Z';
      async.sendBuffer(0, largeBuffer.size());

      const Parameter parameter;
      async.call(parameter);

      async.wait();

      delete[] buffer;
    }
  }

  void testPartialInit() {
    Executor<TestMPI> executor(this);

    MPITest<Executor<TestMPI>, Parameter, Parameter>::Type async;
    async.setScheduler(*m_scheduler);

    if (m_scheduler->isExecutor()) {
      async.setExecutor(executor);

      m_scheduler->loop();
    }

    // setExecutor is not called on non-executors
    // This should not timeout
  }
};
