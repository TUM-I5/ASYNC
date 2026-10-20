// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#include "async/ExecInfo.h"
#include <array>
#include <cstddef>
#include <optional>
#ifdef USE_MPI
#include <mpi.h>
#endif // USE_MPI

#include <cxxtest/TestSuite.h>

#include <pthread.h>
#include <sched.h>

#include "async/Config.h"
#include "async/Dispatcher.h"
#include "async/Module.h"
#include "async/as/Pin.h"

struct Param {
  int step{0};
};

/**
 * The settings the test binary runs with (the mode is chosen through the environment)
 */
inline auto testConfig() -> const async::Config& {
  static const async::Config Config;
  return Config;
}

/**
 * Whether this rank runs the executor callbacks
 */
inline auto executes(const async::Dispatcher& dispatcher) -> bool {
  return testConfig().mode() != async::Mode::MPI || dispatcher.isExecutor();
}

/**
 * The number of ranks whose buffers the executor of this rank holds. The
 * tests run on a single node, so all ranks form one group.
 */
inline auto contributors(int size) -> int {
  if (testConfig().mode() != async::Mode::MPI) {
    return 1;
  }
  return testConfig().groupLayout().exclusive ? size - 1 : size;
}

/**
 * Whether modules can be attached after the dispatcher started the executors
 */
inline auto lateModules() -> bool {
  return testConfig().mode() != async::Mode::MPI || !testConfig().groupLayout().exclusive;
}

class Module : private async::Module<Module, Param, Param> {
  public:
  bool mSetUp{false};
  bool mExecInit{false};
  bool mExec{false};
  bool mTearDown{false};
  int mExecCommSize{0};

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

  void exec(const async::ExecInfo& info, const Param& param) {
    mExec = true;
#ifdef USE_MPI
    MPI_Comm_size(info.comm(), &mExecCommSize);
#endif // USE_MPI
  }

  void setUp() override {
    setExecutor(*this);
    mSetUp = true;
  }

  void tearDown() override { mTearDown = true; }
};

class BufferModule : private async::Module<BufferModule, Param, Param> {
  public:
  std::size_t mInitBufferSize{0};
  std::size_t mBufferSize{0};
  std::size_t mCloneBufferSize{0};
  std::size_t mCloneSyncBufferSize{0};
  std::size_t mBuffer2Size{0};
  bool mValues{false};
  bool mExec{false};

  BufferModule() = default;

  void attachTo(async::Dispatcher& dispatcher) { dispatcher.attach(*this); }

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

    wait();

    finalize();
  }

  void execInit(const async::ExecInfo& info, const Param& param) {
    mInitBufferSize = info.bufferSize(0);
  }

  void exec(const async::ExecInfo& info, const Param& param) {
    mExec = true;
    mBufferSize = info.bufferSize(1);
    mCloneBufferSize = info.bufferSize(2);
    mCloneSyncBufferSize = info.bufferSize(3);
    mBuffer2Size = info.bufferSize(5);

    mValues = allEqual(info, 1, 42) && allEqual(info, 4, 5) && allEqual(info, 5, 43);
  }

  void setUp() override { setExecutor(*this); }

  void tearDown() override {}

  private:
  static auto allEqual(const async::ExecInfo& info, unsigned int id, int value) -> bool {
    const auto* values = static_cast<const int*>(info.buffer(id));
    for (std::size_t i = 0; i < info.bufferSize(id) / sizeof(int); i++) {
      if (values[i] != value) {
        return false;
      }
    }
    return true;
  }
};

class ResizeBufferModule : private async::Module<ResizeBufferModule, Param, Param> {
  public:
  std::array<std::size_t, 2> mBuffer0Size{};
  std::array<std::size_t, 2> mBuffer1Size{};
  std::array<bool, 2> mValues{};

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

    wait();

    const auto buffer2 = std::array<int, 2>{4, 3};
    resizeBuffer(0, buffer2.data(), 2 * sizeof(int));

    resizeBuffer(1, buffer1.data(), sizeof(int));

    sendBuffer(0);
    sendBuffer(1);

    param.step = 1;
    call(param);

    wait();

    finalize();
  }

  void execInit(const async::ExecInfo& info, const Param& param) {}

  void exec(const async::ExecInfo& info, const Param& param) {
    mBuffer0Size.at(param.step) = info.bufferSize(0);
    mBuffer1Size.at(param.step) = info.bufferSize(1);

    const auto* buffer0 = static_cast<const int*>(info.buffer(0));
    const auto* buffer1 = static_cast<const int*>(info.buffer(1));
    bool valid = true;
    switch (param.step) {
    case 0:
      for (std::size_t i = 0; i < info.bufferSize(0) / sizeof(int); i++) {
        valid = valid && buffer0[i] == 42;
      }
      valid = valid && buffer1[0] == 1 && buffer1[1] == 2;
      break;
    case 1:
      for (std::size_t i = 0; i < info.bufferSize(0) / sizeof(int); i += 2) {
        valid = valid && buffer0[i] == 4 && buffer0[i + 1] == 3;
      }
      valid = valid && buffer1[0] == 1;
      break;
    default:
      valid = false;
      break;
    }
    mValues.at(param.step) = valid;
  }

  void setUp() override { setExecutor(*this); }

  void tearDown() override {}
};

class RemoveBufferModule : private async::Module<RemoveBufferModule, Param, Param> {
  public:
  std::size_t mBuffer0Size{42};
  std::size_t mBuffer1Size{42};
  std::size_t mManagedBufferSize{};

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

    wait();

    finalize();
  }

  void execInit(const async::ExecInfo& info, const Param& param) {}

  void exec(const async::ExecInfo& info, const Param& param) {
    mBuffer0Size = info.bufferSize(0);
    mBuffer1Size = info.bufferSize(1);
    mManagedBufferSize = info.bufferSize(2);
  }

  void setUp() override { setExecutor(*this); }

  void tearDown() override {}
};

/**
 * A buffer that holds nothing at first (e.g. on a rank without data for an
 * output) and grows later
 */
class GrowBufferModule : private async::Module<GrowBufferModule, Param, Param> {
  public:
  static constexpr std::size_t Values = 4;
  static constexpr int Value = 7;

  bool mExecuted{false};
  std::size_t mBufferSize{0};
  bool mValid{false};

  GrowBufferModule() = default;

  void run() {
    init();

    const auto data = std::array<int, Values>{Value, Value, Value, Value};
    addBuffer(data.data(), 0);

    auto param = Param{0};
    callInit(param);
    wait();

    resizeBuffer(0, data.data(), Values * sizeof(int));
    sendBuffer(0);

    param.step = 1;
    call(param);
    wait();

    finalize();
  }

  void execInit(const Param& param) {}

  void exec(const async::ExecInfo& info, const Param& param) {
    if (param.step != 1) {
      return;
    }
    mExecuted = true;
    mBufferSize = info.bufferSize(0);
    const auto* values = static_cast<const int*>(info.buffer(0));
    mValid = values != nullptr;
    for (std::size_t i = 0; mValid && i < mBufferSize / sizeof(int); i++) {
      mValid = values[i] == Value;
    }
  }

  void setUp() override { setExecutor(*this); }

  void tearDown() override {}
};

/**
 * Pins the executor thread to a core, as an application does with the cores
 * its computation leaves free
 */
class AffinityModule : private async::Module<AffinityModule, Param, Param> {
  public:
  static constexpr int Core = 0;

  int mCpu{-1};
  int mMainCpusBefore{0};
  int mMainCpusAfter{0};

  AffinityModule() = default;

  void run() {
    init();

    const auto param = Param{0};
    callInit(param);
    wait();
    call(param);
    wait();

    finalize();
  }

  void execInit(const Param& param) {}

  void exec(const async::ExecInfo& info, const Param& param) { mCpu = sched_getcpu(); }

  void setUp() override {
    setExecutor(*this);

    mMainCpusBefore = callingThreadCpus();
    if (isAffinityNecessary()) {
      async::as::CpuMask mask{};
      CPU_ZERO(&mask.set);
      CPU_SET(Core, &mask.set);
      setAffinityIfNecessary(mask);
    }
    mMainCpusAfter = callingThreadCpus();
  }

  void tearDown() override {}

  private:
  static auto callingThreadCpus() -> int {
    cpu_set_t set;
    pthread_getaffinity_np(pthread_self(), sizeof(set), &set);
    return CPU_COUNT(&set);
  }
};

/**
 * Test for {@link Dispatcher} and {@link Module} since
 * they work closely together.
 */
class TestModule : public CxxTest::TestSuite {
  int m_rank{};
  int m_size{1};

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

    TS_ASSERT_EQUALS(dispatcher.groupLayout().executorsPerNode, 1); // the default
    TS_ASSERT_EQUALS(dispatcher.groupLayout().groupNodes, 1);

    Module module;

    dispatcher.init();

    if (dispatcher.dispatch()) {
      TS_ASSERT(dispatcher.isCompute());

      module.run();
    } else {
      TS_ASSERT(dispatcher.isExecutor());
      TS_ASSERT(!dispatcher.isCompute());
    }

    if (testConfig().mode() == async::Mode::MPI) {
      // the executor is the last rank of the group
      TS_ASSERT_EQUALS(dispatcher.isExecutor(), m_rank + 1 == m_size);
    } else {
      TS_ASSERT(!dispatcher.isExecutor());
    }

    const bool executor = executes(dispatcher);

    dispatcher.finalize();

    TS_ASSERT(module.mSetUp);
    if (executor) {
      TS_ASSERT(module.mExecInit);
      TS_ASSERT(module.mExec);
#ifdef USE_MPI
      // one executor for the group in MPI mode, otherwise every rank runs its own
      TS_ASSERT_EQUALS(module.mExecCommSize, testConfig().mode() == async::Mode::MPI ? 1 : m_size);
#endif // USE_MPI
    }
    TS_ASSERT(module.mTearDown);
  }

  void testBuffer() const {
    async::Dispatcher dispatcher;

    BufferModule module;

    dispatcher.init();

    if (dispatcher.dispatch()) {
      module.run();
    }

    const bool executor = executes(dispatcher);
    const auto group = static_cast<std::size_t>(contributors(m_size));

    dispatcher.finalize();

    if (executor) {
      TS_ASSERT(module.mExec);
      TS_ASSERT_EQUALS(module.mInitBufferSize, 2 * sizeof(int) * group);
      TS_ASSERT_EQUALS(module.mBufferSize, sizeof(int) * group);
      TS_ASSERT_EQUALS(module.mCloneBufferSize, sizeof(int));
      TS_ASSERT_EQUALS(module.mCloneSyncBufferSize, sizeof(long));
      TS_ASSERT_EQUALS(module.mBuffer2Size, sizeof(int) * group);
      TS_ASSERT(module.mValues);
    }
  }

  void testResizeBuffer() const {
    async::Dispatcher dispatcher;

    ResizeBufferModule module;

    dispatcher.init();

    if (dispatcher.dispatch()) {
      module.run();
    }

    const bool executor = executes(dispatcher);
    const auto group = static_cast<std::size_t>(contributors(m_size));

    dispatcher.finalize();

    if (executor) {
      TS_ASSERT_EQUALS(module.mBuffer0Size[0], sizeof(int) * group);
      TS_ASSERT_EQUALS(module.mBuffer0Size[1], 2 * sizeof(int) * group);
      TS_ASSERT_EQUALS(module.mBuffer1Size[0], 2 * sizeof(int));
      TS_ASSERT_EQUALS(module.mBuffer1Size[1], sizeof(int));
      TS_ASSERT(module.mValues[0]);
      TS_ASSERT(module.mValues[1]);
    }
  }

  void testRemoveBuffer() const {
    async::Dispatcher dispatcher;

    RemoveBufferModule module;

    dispatcher.init();

    if (dispatcher.dispatch()) {
      module.run();
    }

    const bool executor = executes(dispatcher);
    const auto group = static_cast<std::size_t>(contributors(m_size));

    dispatcher.finalize();

    if (executor) {
      TS_ASSERT_EQUALS(module.mBuffer0Size, 0);
      TS_ASSERT_EQUALS(module.mBuffer1Size, sizeof(int) * group);
      TS_ASSERT_EQUALS(module.mManagedBufferSize, 0);
    }
  }

  void testGrowEmptyBuffer() const {
    async::Dispatcher dispatcher;

    GrowBufferModule module;

    dispatcher.init();

    if (dispatcher.dispatch()) {
      module.run();
    }

    const bool executor = executes(dispatcher);
    const auto group = static_cast<std::size_t>(contributors(m_size));

    dispatcher.finalize();

    if (executor) {
      TS_ASSERT(module.mExecuted);
      TS_ASSERT_EQUALS(module.mBufferSize, group * GrowBufferModule::Values * sizeof(int));
      TS_ASSERT(module.mValid);
    }
  }

  void testAffinity() const {
    async::Dispatcher dispatcher;

    AffinityModule module;

    dispatcher.init();

    if (dispatcher.dispatch()) {
      module.run();
    }

    const bool pinned = testConfig().mode() == async::Mode::Thread ||
                        (testConfig().mode() == async::Mode::MPI && dispatcher.isExecutor());

    dispatcher.finalize();

    // the thread that asked for the affinity keeps its own
    TS_ASSERT_EQUALS(module.mMainCpusBefore, module.mMainCpusAfter);
    if (pinned) {
      TS_ASSERT_EQUALS(module.mCpu, AffinityModule::Core);
    }
  }

  /**
   * A module created once the executors run, as an application does that
   * sets up its output during the simulation
   */
  void testAttachAfterDispatch() const {
    if (!lateModules()) {
      return;
    }

    async::Dispatcher dispatcher;
    dispatcher.init();

    std::optional<BufferModule> module;
    if (dispatcher.dispatch()) {
      module.emplace();
      module->attachTo(dispatcher);
      module->run();
    }

    const bool executor = executes(dispatcher);
    const auto group = static_cast<std::size_t>(contributors(m_size));

    dispatcher.finalize();

    if (executor) {
      TS_ASSERT(module->mExec);
      TS_ASSERT_EQUALS(module->mBufferSize, sizeof(int) * group);
      TS_ASSERT(module->mValues);
    }
  }

  /**
   * A module that comes after all earlier ones are finalized: the executor
   * has to keep running for it
   */
  void testAttachAfterFinalize() const {
    if (!lateModules()) {
      return;
    }

    async::Dispatcher dispatcher;

    Module first;

    dispatcher.init();

    std::optional<BufferModule> second;
    if (dispatcher.dispatch()) {
      first.run();

      second.emplace();
      second->attachTo(dispatcher);
      second->run();
    }

    const bool executor = executes(dispatcher);

    dispatcher.finalize();

    if (executor) {
      TS_ASSERT(first.mExec);
      TS_ASSERT(second->mExec);
      TS_ASSERT(second->mValues);
    }
  }

  static void testModulesActiveDisabled() {
    async::Dispatcher dispatcher;

    TS_ASSERT_EQUALS(dispatcher.groupLayout().executorsPerNode, 1); // the default
    TS_ASSERT_EQUALS(dispatcher.groupLayout().groupNodes, 1);

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
