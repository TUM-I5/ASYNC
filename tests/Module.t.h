// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifdef USE_MPI
#include <mpi.h>
#endif // USE_MPI

#include <cxxtest/TestSuite.h>

#include <sched.h>

#include "async/Config.h"
#include "async/Dispatcher.h"
#include "async/Module.h"

struct Param {
  int step;
};

class Module : private async::Module<Module, Param, Param> {
  public:
  bool mSetUp{false};
  bool mExecInit{false};
  bool mExec{false};
  bool mTearDown{false};

  int mCpu{};

  Module() = default;

  void run() {
    init();

    const auto param = Param{0};

    callInit(param);

    wait();

    call(param);

    wait();

    finalize();
  }

  void norun() {
    init();
    finalize();
  }

  void execInit(const async::ExecInfo& info, const Param& param) {
    TS_ASSERT_EQUALS(info.numBuffers(), 0);
    mExecInit = true;
  }

  void exec(const Param& param) {
    mExec = true;

    mCpu = sched_getcpu();
  }

  void setUp() override {
    setExecutor(*this);
    mSetUp = true;
  }

  void tearDown() override { mTearDown = true; }
};

class BufferModule : private async::Module<BufferModule, Param, Param> {
  public:
  unsigned int mInitBufferSize{0};
  unsigned int mBufferSize{0};
  unsigned int mCloneBufferSize{0};
  unsigned int mCloneSyncBufferSize{0};
  int mBuffer{};
  int mManagedBuffer{};

  BufferModule() = default;

  void run() {
    init();

    auto initBuffer = std::array<int, 2>();
    addBuffer(initBuffer.data(), 2 * sizeof(int));

    int buffer = 42;
    addBuffer(&buffer, sizeof(int));

    int cloneBuffer = 1;
    addBuffer(&cloneBuffer, sizeof(int), true);

    long addCloneSyncBuffer = 2;
    addSyncBuffer(&addCloneSyncBuffer, sizeof(long), true);

    addBuffer(nullptr, 2 * sizeof(int));

    int buffer2 = 43;
    addBuffer(&buffer2, sizeof(int));

    int* managedBuffer = async::Module<BufferModule, Param, Param>::managedBuffer<int*>(4);
    TS_ASSERT_DIFFERS(managedBuffer, static_cast<int*>(nullptr));

    const auto param = Param{0};

    callInit(param);

    wait();

    sendBuffer(1, sizeof(int));

    managedBuffer[0] = 5;
    managedBuffer[1] = 5;

    sendBuffer(4, 2 * sizeof(int));

    sendBuffer(5);

    call(param);

    if (async::Config::mode() == async::MPI) {
      // Set the params on non-executors
      execInit(param);
      execInternal(param, false);
    }

    wait();

    finalize();
  }

  void execInit(const Param& param) { mInitBufferSize = bufferSize(0); }

  void exec(const async::ExecInfo& info, const Param& param) { execInternal(param); }

  void setUp() override { setExecutor(*this); }

  void tearDown() override {}

  private:
  void execInternal(const Param& param, bool isExecutor = true) {
    mBufferSize = bufferSize(1);
    for (unsigned int i = 0; i < mBufferSize / sizeof(int); i++) {
      TS_ASSERT_EQUALS(42, *(static_cast<const int*>(buffer(1)) + i));
    }
    mCloneBufferSize = bufferSize(2);
    mCloneSyncBufferSize = bufferSize(3);

    const unsigned int bufferSize2 = bufferSize(5);
    TS_ASSERT_EQUALS(bufferSize2, mBufferSize);
    for (unsigned int i = 0; i < bufferSize2 / sizeof(int); i++) {
      TS_ASSERT_EQUALS(43, *(static_cast<const int*>(buffer(5)) + i));
    }

    if (isExecutor) {
      const unsigned int managedBufferSize = bufferSize(4);
      for (unsigned int i = 0; i < managedBufferSize / sizeof(int); i++) {
        TS_ASSERT_EQUALS(*(static_cast<const int*>(buffer(4)) + i), 5);
      }
    }
  }
};

class ResizeBufferModule : private async::Module<ResizeBufferModule, Param, Param> {
  public:
  std::array<unsigned int, 2> mBuffer0Size{};
  std::array<unsigned int, 2> mBuffer1Size{};

  ResizeBufferModule() = default;

  void run() {
    init();

    int buffer0 = 42;
    addBuffer(&buffer0, sizeof(int));

    const auto buffer1 = std::array<int, 2>{1, 2};
    addBuffer(buffer1.data(), 2 * sizeof(int), true);

    auto param = Param{0};

    callInit(param);

    wait();

    sendBuffer(0, sizeof(int));
    sendBuffer(1, 2 * sizeof(int));

    param.step = 0;
    call(param);

    if (async::Config::mode() == async::MPI) {
      // Set the params on non-executors
      execInternal(param, false);
    }

    wait();

    const auto buffer2 = std::array<int, 2>{4, 3};
    resizeBuffer(0, buffer2.data(), 2 * sizeof(int));

    resizeBuffer(1, buffer1.data(), sizeof(int));

    sendBuffer(0);
    sendBuffer(1);

    param.step = 1;
    call(param);

    if (async::Config::mode() == async::MPI) {
      // Set the params on non-executors
      execInternal(param, false);
    }

    wait();

    finalize();
  }

  void execInit(const Param& param) {}

  void exec(const async::ExecInfo& info, const Param& param) { execInternal(param); }

  void setUp() override { setExecutor(*this); }

  void tearDown() override {}

  private:
  void execInternal(const Param& param, bool isExecutor = true) {
    mBuffer0Size.at(param.step) = bufferSize(0);
    mBuffer1Size.at(param.step) = bufferSize(1);
    switch (param.step) {
    case 0:
      TS_ASSERT_EQUALS(*static_cast<const int*>(buffer(0)), 42);
      TS_ASSERT_EQUALS(*static_cast<const int*>(buffer(1)), 1);
      TS_ASSERT_EQUALS(*(static_cast<const int*>(buffer(1)) + 1), 2);
      break;
    case 1:
      TS_ASSERT_EQUALS(*static_cast<const int*>(buffer(0)), 4);
      TS_ASSERT_EQUALS(*(static_cast<const int*>(buffer(0)) + 1), 3);
      TS_ASSERT_EQUALS(*static_cast<const int*>(buffer(1)), 1);
      break;
    }
  }
};

class RemoveBufferModule : private async::Module<RemoveBufferModule, Param, Param> {
  public:
  unsigned int mBuffer0Size{42};
  unsigned int mBuffer1Size{42};
  unsigned int mManagedBufferSize{};

  RemoveBufferModule() = default;

  void run() {
    init();

    int buffer0 = 3;
    addBuffer(&buffer0, sizeof(int));

    int buffer1 = 42;
    addBuffer(&buffer1, sizeof(int));

    addBuffer(nullptr, sizeof(int));

    const auto param = Param{0};

    callInit(param);

    wait();

    removeBuffer(0);
    removeBuffer(2);
    sendBuffer(1, sizeof(int));

    call(param);

    if (async::Config::mode() == async::MPI) {
      // Set the params on non-executors
      execInit(param);
      exec(param);
    }

    wait();

    finalize();
  }

  void execInit(const Param& param) {}

  void exec(const Param& param) {
    mBuffer0Size = bufferSize(0);
    mBuffer1Size = bufferSize(1);
    mManagedBufferSize = bufferSize(2);
  }

  void setUp() override { setExecutor(*this); }

  void tearDown() override {}
};

/**
 * Test for {@link Dispatcher} and {@link Module} since
 * they work closely together.
 */
class TestModule : public CxxTest::TestSuite {
  int m_rank{};
  int m_size{};

  public:
  void setUp() override {
#ifdef USE_MPI
    MPI_Comm_rank(MPI_COMM_WORLD, &m_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &m_size);
#endif // USE_MPI
  }

  void testModuleDispatcher() const {
    async::Dispatcher dispatcher;
#ifdef USE_MPI
    dispatcher.setCommunicator(MPI_COMM_WORLD);
#endif // USE_MPI

    const unsigned int groupSize = dispatcher.groupSize();
    if (async::Config::mode() == async::MPI) {
      TS_ASSERT_EQUALS(groupSize, 64); // the default
    } else {
      TS_ASSERT_EQUALS(groupSize, 1);
    }

    Module module;

    dispatcher.init();

    if (dispatcher.dispatch()) {
      TS_ASSERT(!dispatcher.isExecutor());

      module.run();
    } else {
      TS_ASSERT(dispatcher.isExecutor());

      if (async::Config::mode() == async::MPI) {
        TS_ASSERT_EQUALS(m_rank + 1, m_size);
      } else {
        TS_FAIL("No executors in SYNC and THREAD mode!");
      }
    }

    dispatcher.finalize();

    TS_ASSERT(module.mSetUp);
    if (dispatcher.isExecutor()) {
      TS_ASSERT(module.mExecInit);
      TS_ASSERT(module.mExec);
    }
    TS_ASSERT(module.mTearDown);

    if (async::Config::mode() == async::THREAD) {
      TS_ASSERT_EQUALS(module.mCpu, get_nprocs() - 1);
    }
  }

  void testBuffer() const {
    async::Dispatcher dispatcher;

    BufferModule module;

    dispatcher.init();

    if (dispatcher.dispatch()) {
      module.run();
    }

    unsigned int initBufferSize = 2 * sizeof(int);
    unsigned int bufferSize = sizeof(int);
    const unsigned int cloneBufferSize = sizeof(int);
    const unsigned int cloneSyncBufferSize = sizeof(long);

    if (dispatcher.isExecutor() && async::Config::mode() == async::MPI) {
      initBufferSize *= m_size - 1;
      bufferSize *= m_size - 1;
    }

    dispatcher.finalize();

    TS_ASSERT_EQUALS(module.mInitBufferSize, initBufferSize);
    TS_ASSERT_EQUALS(module.mBufferSize, bufferSize);
    TS_ASSERT_EQUALS(module.mCloneBufferSize, cloneBufferSize);
    TS_ASSERT_EQUALS(module.mCloneSyncBufferSize, cloneSyncBufferSize);
  }

  void testResizeBuffer() const {
    async::Dispatcher dispatcher;

    ResizeBufferModule module;

    dispatcher.init();

    if (dispatcher.dispatch()) {
      module.run();
    }

    unsigned int bufferSize = sizeof(int);

    if (dispatcher.isExecutor() && async::Config::mode() == async::MPI) {
      bufferSize *= m_size - 1;
    }

    dispatcher.finalize();

    TS_ASSERT_EQUALS(module.mBuffer0Size[0], bufferSize);
    TS_ASSERT_EQUALS(module.mBuffer0Size[1], 2 * bufferSize);
    TS_ASSERT_EQUALS(module.mBuffer1Size[0], 2 * sizeof(int));
    TS_ASSERT_EQUALS(module.mBuffer1Size[1], sizeof(int));
  }

  void testRemoveBuffer() const {
    async::Dispatcher dispatcher;

    RemoveBufferModule module;

    dispatcher.init();

    if (dispatcher.dispatch()) {
      module.run();
    }

    unsigned int buffer1Size = sizeof(int);

    if (dispatcher.isExecutor() && async::Config::mode() == async::MPI) {
      buffer1Size *= m_size - 1;
    }

    dispatcher.finalize();

    TS_ASSERT_EQUALS(module.mBuffer0Size, 0);
    TS_ASSERT_EQUALS(module.mBuffer1Size, buffer1Size);
    TS_ASSERT_EQUALS(module.mManagedBufferSize, 0);
  }

  static void testModulesActiveDisabled() {
    async::Dispatcher dispatcher;

    const unsigned int groupSize = dispatcher.groupSize();
    if (async::Config::mode() == async::MPI) {
      TS_ASSERT_EQUALS(groupSize, 64); // the default
    } else {
      TS_ASSERT_EQUALS(groupSize, 1);
    }

    Module module0;
    const Module module1;

    dispatcher.init();

    if (dispatcher.dispatch()) {
      // Run only one module
      module0.run();
    }

    dispatcher.finalize();
  }
};
