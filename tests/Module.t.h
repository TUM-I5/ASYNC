// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#include "async/ExecInfo.h"
#include <array>
#include <cstddef>
#include <cstdlib>
#include <optional>
#ifdef USE_MPI
#include <mpi.h>
#endif // USE_MPI

#include <cxxtest/TestSuite.h>

#include <pthread.h>
#include <sched.h>
#include <vector>

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
 * The number of ranks whose buffers the executor of this rank holds. All
 * ranks form one group: the tests run on a single node, or on as many nodes
 * as a group spans.
 */
inline auto contributors(int size) -> int {
  if (testConfig().mode() != async::Mode::MPI) {
    return 1;
  }
  return testConfig().groupLayout().exclusive ? size - 1 : size;
}

/**
 * The first rank whose buffers the executor of this rank holds: the group
 * starts with the first rank, and every other executor holds its own rank
 */
inline auto firstContributor(int rank) -> int {
  return testConfig().mode() == async::Mode::MPI ? 0 : rank;
}

/**
 * Whether modules can be attached after the dispatcher started the executors
 */
inline auto lateModules() -> bool {
  return testConfig().mode() != async::Mode::MPI || !testConfig().groupLayout().exclusive;
}

/**
 * Node boundaries for a dispatcher: ASYNC_TEST_NODE_RANKS consecutive ranks
 * form a node, so that a single machine can stand in for several nodes.
 * Without it, a node is what shares memory.
 */
class TestNodes {
  public:
  explicit TestNodes([[maybe_unused]] async::Dispatcher& dispatcher) {
#ifdef USE_MPI
    const char* ranks = std::getenv("ASYNC_TEST_NODE_RANKS");
    const int nodeRanks = ranks == nullptr ? 0 : std::atoi(ranks);
    if (nodeRanks > 0) {
      int rank = 0;
      MPI_Comm_rank(MPI_COMM_WORLD, &rank);
      MPI_Comm_split(MPI_COMM_WORLD, rank / nodeRanks, rank, &m_comm);
      dispatcher.setNodeCommunicator(m_comm);
    }
#endif // USE_MPI
  }

  ~TestNodes() {
#ifdef USE_MPI
    if (m_comm != MPI_COMM_NULL) {
      MPI_Comm_free(&m_comm);
    }
#endif // USE_MPI
  }

  TestNodes(const TestNodes&) = delete;
  TestNodes(TestNodes&&) = delete;
  auto operator=(const TestNodes&) -> TestNodes& = delete;
  auto operator=(TestNodes&&) -> TestNodes& = delete;

  private:
#ifdef USE_MPI
  MPI_Comm m_comm{MPI_COMM_NULL};
#endif // USE_MPI
};

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
 * Every rank contributes values of its own, as many as differ from rank to
 * rank: the aggregate has to hold them in the order of the ranks, through a
 * resize, for a buffer of the application and for one ASYNC manages
 */
class OrderModule : private async::Module<OrderModule, Param, Param> {
  public:
  static constexpr int Steps = 2;

  /** The ranks whose contributions the executor holds */
  int mContributors{1};

  std::array<std::size_t, Steps> mSize{};
  std::array<std::size_t, Steps> mManagedSize{};
  std::array<bool, Steps> mOrdered{};

  /** The contributions the buffers are made of, as the executor sees them */
  std::array<std::vector<std::size_t>, Steps> mParts{};
  std::array<std::vector<std::size_t>, Steps> mManagedParts{};

  OrderModule() = default;

  void run(int rank) {
    init();

    // first rank + 1 values of the rank, then two values 100 + rank
    std::vector<int> values(rank + 1, rank);
    addBuffer(values.data(), values.size() * sizeof(int));
    addBuffer(nullptr, values.size() * sizeof(int));

    auto param = Param{0};
    callInit(param);
    wait();

    auto* managed = managedBuffer<int*>(1);
    for (int i = 0; i <= rank; i++) {
      managed[i] = 1000 + rank;
    }
    sendBuffer(0);
    sendBuffer(1);
    call(param);
    wait();

    const std::vector<int> other(2, 100 + rank);
    resizeBuffer(0, other.data(), other.size() * sizeof(int));
    resizeBuffer(1, nullptr, 3 * sizeof(int));
    managed = managedBuffer<int*>(1);
    for (int i = 0; i < 3; i++) {
      managed[i] = 2000 + rank;
    }
    sendBuffer(0);
    sendBuffer(1);
    param.step = 1;
    call(param);
    wait();

    finalize();
  }

  void execInit(const Param& param) {}

  void exec(const async::ExecInfo& info, const Param& param) {
    mSize.at(param.step) = info.bufferSize(0);
    mManagedSize.at(param.step) = info.bufferSize(1);
    mParts.at(param.step) = info.bufferParts(0);
    mManagedParts.at(param.step) = info.bufferParts(1);

    const auto* values = static_cast<const int*>(info.buffer(0));
    const auto* managed = static_cast<const int*>(info.buffer(1));
    bool ordered = values != nullptr && managed != nullptr;
    std::size_t value = 0;
    std::size_t managedValue = 0;
    for (int rank = 0; ordered && rank < mContributors; rank++) {
      const int count = param.step == 0 ? rank + 1 : 2;
      for (int i = 0; i < count; i++) {
        ordered = ordered && values[value++] == (param.step == 0 ? rank : 100 + rank);
      }
      const int managedCount = param.step == 0 ? rank + 1 : 3;
      for (int i = 0; i < managedCount; i++) {
        ordered = ordered && managed[managedValue++] == (param.step == 0 ? 1000 : 2000) + rank;
      }
    }
    mOrdered.at(param.step) = ordered && value * sizeof(int) == info.bufferSize(0) &&
                              managedValue * sizeof(int) == info.bufferSize(1);
  }

  void setUp() override { setExecutor(*this); }

  void tearDown() override {}
};

/**
 * A clone buffer that ASYNC manages: every rank fills its copy, the whole
 * size of it, and keeps what it wrote there; the executor holds the copy of
 * the first rank of its group, also after a resize
 */
class CloneModule : private async::Module<CloneModule, Param, Param> {
  public:
  static constexpr int Steps = 2;
  static constexpr std::array<int, Steps> Counts{4, 64};

  /** The rank whose copy the executor holds */
  int mFirst{0};

  std::array<std::size_t, Steps> mSize{};
  std::array<bool, Steps> mCopy{};

  /** Whether this rank found its copy as it wrote it, after all ranks wrote theirs */
  std::array<bool, Steps> mOwn{};

  CloneModule() = default;

  /**
   * @param ranks The ranks that compute, which all fill their copies before
   *  any of them looks at its own again
   */
  template <typename Barrier>
  void run(int rank, const Barrier& ranks) {
    init();

    addBuffer(nullptr, Counts[0] * sizeof(int), true);

    auto param = Param{0};
    callInit(param);
    wait();

    for (int step = 0; step < Steps; step++) {
      if (step > 0) {
        resizeBuffer(0, nullptr, Counts.at(step) * sizeof(int));
      }
      auto* values = managedBuffer<int*>(0);
      for (int i = 0; i < Counts.at(step); i++) {
        values[i] = value(rank, step, i);
      }
      ranks();
      bool own = true;
      for (int i = 0; i < Counts.at(step); i++) {
        own = own && values[i] == value(rank, step, i);
      }
      mOwn.at(step) = own;
      sendBuffer(0);
      param.step = step;
      call(param);
      wait();
    }

    finalize();
  }

  void execInit(const Param& param) {}

  void exec(const async::ExecInfo& info, const Param& param) {
    mSize.at(param.step) = info.bufferSize(0);

    const auto* values = static_cast<const int*>(info.buffer(0));
    bool copy = values != nullptr;
    for (std::size_t i = 0; copy && i < info.bufferSize(0) / sizeof(int); i++) {
      copy = values[i] == value(mFirst, param.step, static_cast<int>(i));
    }
    mCopy.at(param.step) = copy;
  }

  void setUp() override { setExecutor(*this); }

  void tearDown() override {}

  private:
  static auto value(int rank, int step, int i) -> int {
    return (100000 * rank) + (1000 * step) + i;
  }
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
    const TestNodes nodes(dispatcher);
#ifdef USE_MPI
    dispatcher.setCommunicator(MPI_COMM_WORLD);
#endif // USE_MPI

    // what the environment sets
    TS_ASSERT_EQUALS(dispatcher.groupLayout().executorsPerNode,
                     testConfig().groupLayout().executorsPerNode);
    TS_ASSERT_EQUALS(dispatcher.groupLayout().groupNodes, testConfig().groupLayout().groupNodes);

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
    const TestNodes nodes(dispatcher);

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
    const TestNodes nodes(dispatcher);

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
    const TestNodes nodes(dispatcher);

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
    const TestNodes nodes(dispatcher);

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

  void testOrder() const {
    async::Dispatcher dispatcher;
    const TestNodes nodes(dispatcher);

    OrderModule module;
    module.mContributors = contributors(m_size);

    dispatcher.init();

    if (dispatcher.dispatch()) {
      module.run(m_rank);
    }

    const bool executor = executes(dispatcher);

    dispatcher.finalize();

    if (executor) {
      const auto group = static_cast<std::size_t>(module.mContributors);
      // 1 + 2 + ... + group values at first, then two (three) values each
      TS_ASSERT_EQUALS(module.mSize[0], group * (group + 1) / 2 * sizeof(int));
      TS_ASSERT_EQUALS(module.mManagedSize[0], group * (group + 1) / 2 * sizeof(int));
      TS_ASSERT_EQUALS(module.mSize[1], 2 * group * sizeof(int));
      TS_ASSERT_EQUALS(module.mManagedSize[1], 3 * group * sizeof(int));
      TS_ASSERT(module.mOrdered[0]);
      TS_ASSERT(module.mOrdered[1]);

      // one contribution for every rank, in the order of the ranks
      std::array<std::vector<std::size_t>, OrderModule::Steps> parts;
      std::array<std::vector<std::size_t>, OrderModule::Steps> managedParts;
      for (std::size_t i = 0; i < group; i++) {
        parts[0].push_back((i + 1) * sizeof(int));
        managedParts[0].push_back((i + 1) * sizeof(int));
        parts[1].push_back(2 * sizeof(int));
        managedParts[1].push_back(3 * sizeof(int));
      }
      TS_ASSERT(module.mParts == parts);
      TS_ASSERT(module.mManagedParts == managedParts);
    }
  }

  void testCloneManaged() const {
    async::Dispatcher dispatcher;
    const TestNodes nodes(dispatcher);

    CloneModule module;
    module.mFirst = firstContributor(m_rank);

    dispatcher.init();

#ifdef USE_MPI
    const auto ranks = [&dispatcher]() { MPI_Barrier(dispatcher.commWorld()); };
#else  // USE_MPI
    const auto ranks = []() {};
#endif // USE_MPI

    const bool computes = dispatcher.dispatch();
    if (computes) {
      module.run(m_rank, ranks);
    }

    const bool executor = executes(dispatcher);

    dispatcher.finalize();

    for (int step = 0; step < CloneModule::Steps; step++) {
      if (computes) {
        TS_ASSERT(module.mOwn.at(step));
      }
      if (executor) {
        TS_ASSERT_EQUALS(module.mSize.at(step), CloneModule::Counts.at(step) * sizeof(int));
        TS_ASSERT(module.mCopy.at(step));
      }
    }
  }

  void testAffinity() const {
    async::Dispatcher dispatcher;
    const TestNodes nodes(dispatcher);

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
    const TestNodes nodes(dispatcher);
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
    const TestNodes nodes(dispatcher);

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
    const TestNodes nodes(dispatcher);

    TS_ASSERT_EQUALS(dispatcher.groupLayout().executorsPerNode,
                     testConfig().groupLayout().executorsPerNode);
    TS_ASSERT_EQUALS(dispatcher.groupLayout().groupNodes, testConfig().groupLayout().groupNodes);

    Module module0;
    const Module module1;

    dispatcher.init();

    if (dispatcher.dispatch()) {
      // Run only one module
      module0.run();
    }

    dispatcher.finalize();
  }

  /**
   * Dispatchers one after another that leave a module behind: what the ranks
   * sent the executor of one may not reach the next one, whose communicators
   * can take the place of the freed ones
   */
  void testModulesLeftBehind() const {
    constexpr int Dispatchers = 4;
    for (int i = 0; i < Dispatchers; i++) {
      async::Dispatcher dispatcher;
      const TestNodes nodes(dispatcher);

      Module used;
      const Module left;

      dispatcher.init();

      if (dispatcher.dispatch()) {
        used.run();
      }

      const bool executor = executes(dispatcher);

      dispatcher.finalize();

      if (executor) {
        TS_ASSERT(used.mExec);
      }
    }
  }
};
