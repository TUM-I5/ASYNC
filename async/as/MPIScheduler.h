// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#ifndef ASYNC_AS_MPISCHEDULER_H
#define ASYNC_AS_MPISCHEDULER_H

#include <mpi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <pthread.h>
#include <thread>
#include <utility>
#include <vector>

#include "async/Config.h"
#include "async/as/Pin.h"
#include "utils/logger.h"

namespace async::as {

/**
 * A pair of MPI requests
 */
struct MPIRequest2 {
  std::array<MPI_Request, 2> r;
};

/**
 * Escalating wait for a poll loop.
 *
 * Spins first, then yields, then sleeps for an interval that doubles up to a
 * cap. A blocking MPI call would look cheaper on a rank that has nothing else
 * to do, but most implementations spin inside it: the process of an idle
 * executor keeps a whole core busy. Next to the computation, that core is one
 * the application needs.
 *
 * Short pauses right after a message keep the latency of a burst low; the
 * growing sleep keeps an executor that waits for the next output cheap.
 */
class Backoff {
  private:
  static const unsigned int SpinRounds = 64;
  static const unsigned int YieldRounds = 1024;
  static const long FirstSleepUs = 50;

  long m_maxSleepUs;

  unsigned int m_idle{0};

  long m_sleepUs{FirstSleepUs};

  public:
  /**
   * @param maxSleepUs The longest sleep between two polls, in microseconds; 0
   *  never sleeps and keeps yielding instead
   */
  explicit Backoff(long maxSleepUs) : m_maxSleepUs(maxSleepUs) {}

  void pause() {
    ++m_idle;

    if (m_idle <= SpinRounds) {
      return;
    }

    if (m_idle <= YieldRounds || m_maxSleepUs <= 0) {
      std::this_thread::yield();
      return;
    }

    const auto sleepUs = std::min(m_sleepUs, m_maxSleepUs);
    std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
    m_sleepUs = std::min(2 * m_sleepUs, m_maxSleepUs);
  }
};

template <class Executor, typename InitParameter, typename Parameter>
class MPIBase;
template <class Executor, typename InitParameter, typename Parameter>
class MPI;
template <class Executor, typename InitParameter, typename Parameter>
class MPIAsync;
class MPIScheduler;

/**
 * The contribution of a co-located executor rank to one of its own buffers.
 *
 * Where the executor takes part in the computation, its data never travels
 * over MPI. The scheduler hands it to the executor side directly instead.
 */
struct LocalBuffer {
  const void* origin{nullptr};
  size_t size{0};
  bool clone{false};
  bool valid{false};
};

class Scheduled {
  friend class MPIScheduler;

  private:
  std::vector<std::byte> m_paramBuffer;

  protected:
  Scheduled() = default;

  ~Scheduled() = default;

  private:
  void allocParamBuffer() { m_paramBuffer.resize(paramSize()); }

  auto paramBuffer() -> void* { return m_paramBuffer.data(); }

  [[nodiscard]] auto paramBuffer() const -> const void* { return m_paramBuffer.data(); }

  /**
   * @return The maximum buffer size required to store init parameter
   *  or parameter
   */
  [[nodiscard]] virtual auto paramSize() const -> unsigned int = 0;

  /**
   * @return True if this class uses async copying
   */
  [[nodiscard]] virtual auto useAsyncCopy() const -> bool = 0;

  /**
   * @return True if this class uses async copying for a specific buffer
   */
  [[nodiscard]] virtual auto useAsyncCopy(unsigned int id) const -> bool = 0;

  /**
   * Returns the number of buffer chunks send to the executor
   *
   * This might differ from the number of buffers since large buffers
   * have the be send in multiple iterations.
   */
  [[nodiscard]] virtual auto numBufferChunks() const -> unsigned int = 0;

  /**
   * @param sync True if the buffer is send sychronized
   * @param local The contribution of the executor rank itself
   */
  virtual void addBufferInternal(bool sync, const LocalBuffer& local) = 0;

  virtual void resizeBufferInternal(unsigned int id, const LocalBuffer& local) = 0;

  virtual void removeBufferInternal(unsigned int id) = 0;

  virtual void execInitInternal(const void* parameter) = 0;

  virtual auto getBufferPos(unsigned int id, int rank, int size) -> void* = 0;

  virtual void execInternal(const void* parameter) = 0;

  /**
   * Wait on the executor for the call to finish
   */
  virtual void waitInternal() = 0;

  virtual void finalizeInternal() = 0;
};

/**
 * Scheduler for asynchronous MPI calls
 *
 * @warning Only one instance of this class should be created
 */
// (the members are grouped by what they are for; there is only one instance, so its padding does
// not matter)
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
class MPIScheduler {
  template <class Executor, typename InitParameter, typename Parameter>
  friend class MPIBase;
  template <class Executor, typename InitParameter, typename Parameter>
  friend class MPI;
  template <class Executor, typename InitParameter, typename Parameter>
  friend class MPIAsync;

  private:
  /** The group local communicator (incl. the executor) */
  MPI_Comm m_privateGroupComm;

  /** The public group local communicator (excl. the executor) */
  MPI_Comm m_groupComm;

  /** The rank of in the executor group */
  int m_groupRank{0};

  /** The size of the executor group (incl. the executor) */
  int m_groupSize{0};

  /** The size of the executor group (incl. the executor) */
  int m_executorRank{0};

  /** True of this task is an executor */
  bool m_isExecutor{false};

  /** True of this task is a compute rank */
  bool m_isCompute{true};

  /** True if the executor of this group is barred from the computation */
  bool m_executorExclusive{true};

  /** Number of ranks of the group that take part in the computation */
  int m_numComputeRanks{0};

  /** What the configuration has set up, over all groups */
  ExecutorLayout m_layout;

  /** Whether groups that share memory may aggregate in shared windows */
  bool m_allowSharedMemory{true};

  /** Whether this group aggregates in shared windows */
  bool m_sharedMemory{false};

  /**
   * The ranks of the group on the node of this rank; they share a window.
   * The group ranks of a node are consecutive.
   */
  MPI_Comm m_nodeComm{MPI_COMM_NULL};

  /** The node the group ranks are on, numbered within the group */
  std::vector<int> m_nodeOfGroupRank;

  /** Node boundaries drawn by the caller, for the group that is being formed */
  int m_pendingNode{-1};

  /** Set while the executor shuts down modules without their ranks */
  bool m_killing{false};

  /** The "COMM_WORLD" communicator (communicator without executors) */
  MPI_Comm m_commWorld;

  /**
   * All async objects, by id.
   *
   * On an executor that computes as well, the loop thread owns this list once
   * it runs: later modules reach it through the local queue.
   */
  std::vector<Scheduled*> m_asyncCalls;

  /** The id the next module gets; the same sequence on every rank of a group */
  int m_nextId{0};

  /** Modules this rank computes for and has not finalized yet */
  int m_liveCalls{0};

  /**
   * Whether the dispatcher has started the executors. A module that comes
   * later registers with the loop of its executor, which is running by then.
   */
  bool m_dispatched{false};

  /** Managed buffer counters for each size */
  std::map<size_t, unsigned int> m_managedBufferCounter;

  /** Managed buffer size */
  std::vector<uint8_t> m_managedBuffer;

  /**
   * A list of possible buffer ids
   *
   * With MPI_Isend we need to provide a memory location for buffer ids
   * that is not on the stack. We can use this list which contains:
   * id[0] = 0, id[1] = 1, ...
   *
   * An id has to stay where it is until its send completes, also while other
   * buffers are added, hence a deque.
   */
  std::deque<unsigned int> m_heapBufferIds;

  /** Class is finalized? */
  bool m_finalized{false};

  /** Sleep between two probes of an idle executor, in microseconds */
  long m_pollIntervalUs{-1};

  /** The thread running {@link loop} while the main thread computes */
  pthread_t m_loopThread{};

  bool m_loopThreadRunning{false};

  std::optional<CpuMask> m_loopAffinity;

  /**
   * Requests that the compute thread of a co-located executor rank hands to
   * the loop thread instead of sending them to itself.
   *
   * Everything the protocol does on the group communicator has to be issued
   * once per rank. On a rank that is both executor and compute rank, the loop
   * thread issues it, on behalf of both roles.
   */
  struct LocalRequest {
    int tag{0};
    int id{0};
    bool sync{true};
    unsigned int bufferId{0};
    const void* origin{nullptr};
    size_t size{0};
    bool clone{false};
    std::vector<std::byte> param;
    /** The module to register (RegisterTag) */
    Scheduled* scheduled{nullptr};
    /** Whether this rank leaves modules behind, which the executor then shuts down (ShutdownTag) */
    bool kill{false};
    std::shared_ptr<bool> done;
  };

  std::mutex m_localMutex;

  std::condition_variable m_localDone;

  std::deque<LocalRequest> m_localQueue;

  /** Size of the queue, so that the poll loop can skip the lock */
  std::atomic<int> m_localPending{0};

  std::shared_ptr<bool> m_pendingLocal;

  /** The operation the outstanding local request belongs to */
  int m_pendingId{-1};
  int m_pendingTag{-1};

  /** Set once the loop thread is gone; local requests return right away from then on */
  bool m_loopDone{false};

  public:
  MPIScheduler()
      : m_privateGroupComm(MPI_COMM_NULL), m_groupComm(MPI_COMM_SELF), m_commWorld(MPI_COMM_WORLD) {
  }

  ~MPIScheduler() { finalize(); }

  auto operator=(MPIScheduler&&) -> MPIScheduler& = delete;
  auto operator=(const MPIScheduler&) -> MPIScheduler& = delete;
  MPIScheduler(const MPIScheduler&) = delete;
  MPIScheduler(MPIScheduler&&) = delete;

  /**
   * Set the MPI configuration.
   *
   * Has to be called before {@link init()}
   *
   * @param comm The MPI communicator that should be used
   * @param groupSize Number of ranks per group (excluding the executor)
   * @deprecated Use the {@link GroupLayout} overload, which keeps groups
   *  inside a node instead of assuming a block-wise rank distribution
   */
  void setCommunicator(MPI_Comm comm, unsigned int groupSize) {
    int rank = 0;
    MPI_Comm_rank(comm, &rank);

    MPI_Comm groupComm = MPI_COMM_NULL;

    // Create group communicator
    MPI_Comm_split(comm, static_cast<int>(rank / (groupSize + 1)), 0, &groupComm);

    MPI_Comm_rank(groupComm, &m_groupRank);
    MPI_Comm_size(groupComm, &m_groupSize);

    m_executorRank = m_groupSize - 1;

    const auto isExecutor = m_groupRank == m_executorRank;

    // currently, executors need to be exclusive
    setCommunicator(comm, groupComm, isExecutor, true);
  }

  /**
   * Set the MPI configuration, forming the groups along node boundaries.
   *
   * Has to be called before {@link init()}
   *
   * @param comm The MPI communicator that should be used
   * @param layout How many executors to place per node, and how many nodes
   *  share one set of executors
   */
  void setCommunicator(MPI_Comm comm, const GroupLayout& layout) {
    MPI_Comm nodeComm = MPI_COMM_NULL;
    MPI_Comm_split_type(comm, MPI_COMM_TYPE_SHARED, 0, MPI_INFO_NULL, &nodeComm);

    setCommunicator(comm, nodeComm, layout);

    MPI_Comm_free(&nodeComm);
  }

  /**
   * Set the MPI configuration, forming the groups along the boundaries of
   * the given nodes.
   *
   * @param comm The MPI communicator that should be used
   * @param nodeComm The ranks of comm that share a node with this one (the
   *  shared-memory split, except where a test or an application wants to
   *  draw the boundaries itself)
   * @param layout How many executors to place per node, and how many nodes
   *  share one set of executors
   */
  void setCommunicator(MPI_Comm comm, MPI_Comm nodeComm, const GroupLayout& layout) {
    int rank = 0;
    MPI_Comm_rank(comm, &rank);

    int nodeRank = 0;
    int nodeSize = 0;
    MPI_Comm_rank(nodeComm, &nodeRank);
    MPI_Comm_size(nodeComm, &nodeSize);

    // A dense node index, ordered by the first rank of each node
    int nodeId = 0;
    const int isNodeRoot = (nodeRank == 0) ? 1 : 0;
    MPI_Exscan(&isNodeRoot, &nodeId, 1, MPI_INT, MPI_SUM, comm);
    if (rank == 0) {
      // MPI_Exscan leaves the result undefined on the first rank
      nodeId = 0;
    }
    MPI_Bcast(&nodeId, 1, MPI_INT, 0, nodeComm);

    // The stride between the group ids of a node has to be the same everywhere,
    // so agree on the largest number of executors any node asks for
    auto executorsPerNode = std::max(1U, layout.executorsPerNode);
    if (layout.maxGroupRanks > 0) {
      const auto required =
          (static_cast<unsigned int>(nodeSize) + layout.maxGroupRanks - 1) / layout.maxGroupRanks;
      executorsPerNode = std::max(executorsPerNode, required);
    }
    MPI_Allreduce(MPI_IN_PLACE, &executorsPerNode, 1, MPI_UNSIGNED, MPI_MAX, comm);

    const auto groupNodes = std::max(1U, layout.groupNodes);

    const auto localGroup = (static_cast<unsigned int>(nodeRank) * executorsPerNode) /
                            static_cast<unsigned int>(nodeSize);
    const auto groupId =
        (static_cast<unsigned int>(nodeId) / groupNodes) * executorsPerNode + localGroup;

    // order the group node by node, so that the ranks of a node are consecutive in it
    int maxNodeSize = nodeSize;
    MPI_Allreduce(MPI_IN_PLACE, &maxNodeSize, 1, MPI_INT, MPI_MAX, comm);
    const int key =
        static_cast<int>(static_cast<unsigned int>(nodeId) % groupNodes) * maxNodeSize + nodeRank;

    MPI_Comm groupComm = MPI_COMM_NULL;
    MPI_Comm_split(comm, static_cast<int>(groupId), key, &groupComm);

    // the node, as the caller draws the boundaries
    m_pendingNode = nodeId;

    MPI_Comm_rank(groupComm, &m_groupRank);
    MPI_Comm_size(groupComm, &m_groupSize);

    m_executorRank = m_groupSize - 1;

    if (layout.exclusive && m_groupSize < 2) {
      logError() << "ASYNC: A group of" << m_groupSize
                 << "rank(s) leaves no rank for the computation. Lower EXECUTORS_PER_NODE,"
                 << "raise MAX_GROUP_RANKS, or let the executor compute as well.";
    }

    setCommunicator(comm, groupComm, m_groupRank == m_executorRank, layout.exclusive);

    int nodes = nodeId + 1;
    MPI_Allreduce(MPI_IN_PLACE, &nodes, 1, MPI_INT, MPI_MAX, comm);
    m_layout.nodes = nodes;
    m_layout.executorsPerNode = executorsPerNode;
    m_layout.groupNodes = groupNodes;
  }

  /**
   * Set the MPI configuration.
   *
   * Has to be called before {@link init()}
   *
   * @param comm The global MPI communicator that should be used
   * @param groupComm The group MPI communicator that should be used
   * @param isExecutor Indicates if the current rank is an executor
   * @param executorExclusive Indicates if the executor should be excluded from the group
   */
  void setCommunicator(MPI_Comm comm, MPI_Comm groupComm, bool isExecutor, bool executorExclusive) {
    m_privateGroupComm = groupComm;

    // Get group rank/size
    MPI_Comm_rank(groupComm, &m_groupRank);
    MPI_Comm_size(groupComm, &m_groupSize);

    // Is an executor?
    m_isExecutor = isExecutor;
    m_executorRank = m_groupSize - 1;
    m_isCompute = !(executorExclusive && isExecutor);
    m_executorExclusive = executorExclusive;

    // The executor is the last rank of the group, so the compute ranks are
    // always the first m_numComputeRanks ones
    m_numComputeRanks = executorExclusive ? m_groupSize - 1 : m_groupSize;

    // The part of the group on this node; without boundaries from the caller,
    // the ranks that share memory
    if (m_pendingNode >= 0) {
      MPI_Comm_split(groupComm, m_pendingNode, m_groupRank, &m_nodeComm);
      m_pendingNode = -1;
    } else {
      MPI_Comm_split_type(groupComm, MPI_COMM_TYPE_SHARED, m_groupRank, MPI_INFO_NULL, &m_nodeComm);
    }
    int nodeRank = 0;
    MPI_Comm_rank(m_nodeComm, &nodeRank);

    // number the nodes within the group by their first rank
    int node = 0;
    const int isNodeRoot = nodeRank == 0 ? 1 : 0;
    MPI_Exscan(&isNodeRoot, &node, 1, MPI_INT, MPI_SUM, groupComm);
    if (m_groupRank == 0) {
      // MPI_Exscan leaves the result undefined on the first rank
      node = 0;
    }
    MPI_Bcast(&node, 1, MPI_INT, 0, m_nodeComm);
    m_nodeOfGroupRank.resize(m_groupSize);
    MPI_Allgather(&node, 1, MPI_INT, m_nodeOfGroupRank.data(), 1, MPI_INT, groupComm);

    // A node part can aggregate in a shared window if its ranks do share
    // memory, which may be left to check where the caller drew the boundaries
    int sharedMemory = m_allowSharedMemory ? 1 : 0;
    if (sharedMemory != 0) {
      MPI_Comm sharedComm = MPI_COMM_NULL;
      MPI_Comm_split_type(m_nodeComm, MPI_COMM_TYPE_SHARED, 0, MPI_INFO_NULL, &sharedComm);
      int sharedSize = 0;
      int nodeSize = 0;
      MPI_Comm_size(sharedComm, &sharedSize);
      MPI_Comm_size(m_nodeComm, &nodeSize);
      MPI_Comm_free(&sharedComm);
      sharedMemory = sharedSize == nodeSize ? 1 : 0;
    }
    MPI_Allreduce(MPI_IN_PLACE, &sharedMemory, 1, MPI_INT, MPI_LAND, groupComm);
    m_sharedMemory = sharedMemory != 0;

    if (m_pollIntervalUs < 0) {
      // An exclusive executor owns its rank and can spin; a co-located one
      // shares its cores with the computation and has to step back
      m_pollIntervalUs = executorExclusive ? 0 : DefaultPollIntervalUs;
    }

    int executorCount = isExecutor ? 1 : 0;
    MPI_Allreduce(MPI_IN_PLACE, &executorCount, 1, MPI_INT, MPI_SUM, groupComm);

    if (executorCount != 1) {
      logError() << "ASYNC: A group needs exactly one executor; this one has" << executorCount;
    }

    int misplaced = (isExecutor != (m_groupRank == m_executorRank)) ? 1 : 0;
    MPI_Allreduce(MPI_IN_PLACE, &misplaced, 1, MPI_INT, MPI_MAX, groupComm);
    if (misplaced != 0) {
      logError() << "ASYNC: The executor has to be the last rank of its group.";
    }

    // what came out of it, over all groups
    std::array<int, 3> counts{isExecutor ? 1 : 0,
                              isExecutor ? m_numComputeRanks : std::numeric_limits<int>::max(),
                              isExecutor ? m_numComputeRanks : 0};
    MPI_Allreduce(MPI_IN_PLACE, counts.data(), 1, MPI_INT, MPI_SUM, comm);
    MPI_Allreduce(MPI_IN_PLACE, &counts[1], 1, MPI_INT, MPI_MIN, comm);
    MPI_Allreduce(MPI_IN_PLACE, &counts[2], 1, MPI_INT, MPI_MAX, comm);
    m_layout.mode = Mode::MPI;
    m_layout.executors = counts[0];
    m_layout.exclusive = executorExclusive;
    m_layout.minGroupRanks = counts[1];
    m_layout.maxGroupRanks = counts[2];

    if (executorExclusive) {
      // Create the new comm world communicator
      MPI_Comm_split(comm, (m_isExecutor ? 1 : 0), 0, &m_commWorld);

      // Create the public group communicator (excl. the executor)
      MPI_Comm_split(m_privateGroupComm, (m_isExecutor ? MPI_UNDEFINED : 0), 0, &m_groupComm);
    } else {
      // Everybody computes: the application keeps its communicator, and the
      // compute ranks of the group are all of it
      MPI_Comm_dup(comm, &m_commWorld);
      MPI_Comm_dup(groupComm, &m_groupComm);
    }
  }

  /**
   * How long the executor sleeps between two probes once it has run out of
   * work. A negative value selects the default for the current layout.
   */
  void setPollIntervalUs(long pollIntervalUs) { m_pollIntervalUs = pollIntervalUs; }

  /**
   * Whether groups whose ranks share memory may aggregate in shared windows;
   * has to be set before the communicator
   */
  void setAllowSharedMemory(bool allow) { m_allowSharedMemory = allow; }

  /**
   * @return Whether this group aggregates in shared windows
   */
  [[nodiscard]] auto sharedMemory() const -> bool { return m_sharedMemory; }

  /**
   * @return The ranks of the group on the node of this rank
   */
  [[nodiscard]] auto nodeComm() const -> MPI_Comm { return m_nodeComm; }

  /**
   * @return The node of a group rank, numbered within the group; the ranks of
   *  a node are consecutive
   */
  [[nodiscard]] auto nodeOf(int groupRank) const -> int { return m_nodeOfGroupRank[groupRank]; }

  /**
   * @return Whether this rank is on the node of the executor, where the node
   *  window is the aggregate itself
   */
  [[nodiscard]] auto onExecutorNode() const -> bool {
    return m_nodeOfGroupRank[m_groupRank] == m_nodeOfGroupRank[m_executorRank];
  }

  /**
   * @return Whether this rank forwards the window of its node to the executor:
   *  the last rank of a node other than the one of the executor
   */
  [[nodiscard]] auto isForwarder() const -> bool {
    return !onExecutorNode() &&
           (m_groupRank + 1 == m_groupSize ||
            m_nodeOfGroupRank[m_groupRank + 1] != m_nodeOfGroupRank[m_groupRank]);
  }

  /**
   * @return Whether the executor shuts the modules down without their ranks,
   *  which then take no part in it
   */
  [[nodiscard]] auto killing() const -> bool { return m_killing; }

  /**
   * @return Whether the dispatcher is gone, together with the group
   */
  [[nodiscard]] auto finalized() const -> bool { return m_finalized; }

  /**
   * Called on every rank when the dispatcher starts the executors; modules
   * attached afterwards register with the running executor
   */
  void setDispatched() { m_dispatched = true; }

  [[nodiscard]] auto groupRank() const -> int { return m_groupRank; }

  /**
   * @return What has been set up, over all groups
   */
  [[nodiscard]] auto layout() const -> const ExecutorLayout& { return m_layout; }

  /**
   * Find out whether the executors hold the output in the rank order of the
   * ranks that compute.
   *
   * @param execComm The communicator over the executors, ordered like them
   */
  void determineOrder(MPI_Comm execComm) {
    // where the output of this group starts in the order of the executors
    long groupOffset = 0;
    if (m_isExecutor) {
      long computeRanks = m_numComputeRanks;
      int execRank = 0;
      MPI_Comm_rank(execComm, &execRank);
      MPI_Exscan(&computeRanks, &groupOffset, 1, MPI_LONG, MPI_SUM, execComm);
      if (execRank == 0) {
        // MPI_Exscan leaves the result undefined on the first rank
        groupOffset = 0;
      }
    }
    MPI_Bcast(&groupOffset, 1, MPI_LONG, m_executorRank, m_privateGroupComm);

    // the compute ranks of a group are its first ranks, in their order
    int ordered = 1;
    if (m_isCompute) {
      int computeRank = 0;
      MPI_Comm_rank(m_commWorld, &computeRank);
      ordered = (groupOffset + m_groupRank == computeRank) ? 1 : 0;
    }
    MPI_Allreduce(MPI_IN_PLACE, &ordered, 1, MPI_INT, MPI_LAND, m_privateGroupComm);
    // the executors speak for their groups, which partition the communicator
    if (m_isExecutor) {
      MPI_Allreduce(MPI_IN_PLACE, &ordered, 1, MPI_INT, MPI_LAND, execComm);
    }
    MPI_Bcast(&ordered, 1, MPI_INT, m_executorRank, m_privateGroupComm);
    m_layout.rankOrdered = ordered != 0;
  }

  [[nodiscard]] auto isExecutor() const -> bool { return m_isExecutor; }

  /**
   * @return True if this rank takes part in the computation
   */
  [[nodiscard]] auto isCompute() const -> bool { return m_isCompute; }

  /**
   * @return True if this rank is an executor and a compute rank at once
   */
  [[nodiscard]] auto isColocated() const -> bool { return m_isExecutor && m_isCompute; }

  /**
   * @return The number of ranks of this group that take part in the computation
   */
  [[nodiscard]] auto numComputeRanks() const -> int { return m_numComputeRanks; }

  /**
   * Affinity for the loop thread of a co-located executor. Has to be set
   * before {@link startLoopThread}.
   */
  void setAffinity(const CpuMask& mask) { m_loopAffinity = mask; }

  [[nodiscard]] auto commWorld() const -> MPI_Comm { return m_commWorld; }

  [[nodiscard]] auto groupComm() const -> MPI_Comm { return m_groupComm; }

  /**
   * Should be called on the executor to start the execution loop.
   *
   * On an exclusive executor this returns once all modules have been
   * finalized. A co-located executor runs it on {@link startLoopThread}
   * instead, so that the calling thread can go on computing; there it returns
   * once the dispatcher is finalized and all modules are.
   */
  void loop() {
    if (!m_isExecutor) {
      return;
    }

    /** What has arrived of the operation a module is on */
    struct CallState {
      unsigned int ready{0};
      unsigned int asyncReady{0};
      /** Distinguishes between init and param tag (required for asynchronous copies) */
      int lastTag{0};
      /** The contribution of this rank, where it computes as well */
      LocalBuffer local;
    };
    std::vector<CallState> calls(m_asyncCalls.size());
    const auto state = [&calls](int id) -> CallState& {
      if (id >= static_cast<int>(calls.size())) {
        calls.resize(id + 1);
      }
      return calls[id];
    };
    const auto known = [this](int id) {
      return id >= 0 && id < static_cast<int>(m_asyncCalls.size()) && m_asyncCalls[id] != nullptr;
    };
    const auto bufferChunks = [this, &known](int id) -> unsigned int {
      // (a module whose registration is still under way has no buffers yet)
      return known(id) ? m_asyncCalls[id]->numBufferChunks() : 0;
    };

    // Number of finalized aync calls
    unsigned int finalized = 0;

    // Whether the dispatcher is done (only ever set where the executor computes as well)
    bool shutdown = false;

    // Modules that ranks leave behind are shut down once every rank of the group is done: it has
    // ended, with the kill switch or the shutdown of the own compute thread as the last thing it
    // hands over, or it has finalized every module. Until then, what it sent may still be on the
    // way; arriving after the loop, it would reach whatever communicator takes the place of the
    // freed one.
    bool killing = false;
    std::vector<char> ended(m_groupSize, 0);
    std::vector<std::size_t> finalizes(m_groupSize, 0);
    const auto everybodyDone = [&]() {
      for (int rank = 0; rank < m_groupSize; rank++) {
        const bool computes = rank != m_executorRank || m_isCompute;
        if (computes && ended[rank] == 0 && finalizes[rank] < m_asyncCalls.size()) {
          return false;
        }
      }
      return true;
    };

    const auto done = [&]() {
      if (killing) {
        return everybodyDone();
      }
      const bool allFinalized = finalized == m_asyncCalls.size();
      return m_executorExclusive ? allFinalized : (shutdown && allFinalized);
    };

    while (!done()) {
      int id = 0;
      int tag = 0;

      int sync = 0;              // Required for add tag
      unsigned int bufferId = 0; // Required for remove tag and buffer tag

      bool complete = false;
      while (!complete) {
        LocalRequest local;
        MPI_Status status;
        MPI_Message message = MPI_MESSAGE_NULL;

        if (pollNext(local, message, status)) {
          if (local.tag == ShutdownTag) {
            shutdown = true;
            ended[m_groupRank] = 1;
            killing = killing || local.kill;
            break;
          }

          id = local.id;
          tag = local.tag;
          auto& call = state(id);

          if (tag == RegisterTag) {
            if (id >= static_cast<int>(m_asyncCalls.size())) {
              m_asyncCalls.resize(id + 1, nullptr);
            }
            m_asyncCalls[id] = local.scheduled;
          } else if (!known(id)) {
            logError() << "ASYNC: Invalid id" << id << "handed over locally";
          }

          switch (tag) {
          case FinalizeTag:
            finalizes[m_groupRank]++;
            bufferId = local.bufferId;
            call.ready++;
            break;
          case RegisterTag:
          case RemoveTag:
          case WaitTag:
            bufferId = local.bufferId;
            call.ready++;
            break;
          case AddTag:
            sync = static_cast<int>(local.sync);
            call.local = LocalBuffer{local.origin, local.size, local.clone, true};
            call.ready++;
            break;
          case ResizeTag:
            bufferId = local.bufferId;
            call.local = LocalBuffer{local.origin, local.size, local.clone, true};
            call.ready++;
            break;
          case InitTag:
          case ParamTag:
            memcpy(m_asyncCalls[id]->paramBuffer(), local.param.data(), local.param.size());

            call.lastTag = tag;
            if (m_asyncCalls[id]->useAsyncCopy()) {
              call.asyncReady++;
            } else {
              call.ready++;
            }
            break;
          default:
            logError() << "ASYNC: Unknown tag" << tag << "handed over locally";
          }

          if (local.done) {
            m_pendingLocal = std::move(local.done);
            m_pendingId = id;
            m_pendingTag = tag;
          }
        } else {
          if (status.MPI_TAG == KillTag) {
            // the rank leaves modules behind, and sends nothing after this
            MPI_Mrecv(nullptr, 0, MPI_BYTE, &message, MPI_STATUS_IGNORE);
            ended[status.MPI_SOURCE] = 1;
            killing = true;
            break;
          }

          tag = status.MPI_TAG - NumStaticTags;
          id = tag / NumTags;
          tag = tag % NumTags;

          // a registration may arrive from another rank before the one of this rank
          if (tag != RegisterTag && !known(id)) {
            logError() << "ASYNC: Invalid id" << id << "received";
          }

          auto& call = state(id);
          int size = 0;
          void* buf = nullptr;

          switch (tag) {
          case AddTag:
            MPI_Mrecv(&sync, 1, MPI_INT, &message, MPI_STATUS_IGNORE);

            call.ready++;
            break;
          case ResizeTag:
          case RemoveTag:
            MPI_Mrecv(&bufferId, 1, MPI_UNSIGNED, &message, MPI_STATUS_IGNORE);

            call.ready++;
            break;
          case InitTag:
          case ParamTag:
            MPI_Get_count(&status, MPI_BYTE, &size);
            MPI_Mrecv(m_asyncCalls[id]->paramBuffer(), size, MPI_BYTE, &message, MPI_STATUS_IGNORE);

            call.lastTag = tag;
            if (m_asyncCalls[id]->useAsyncCopy()) {
              call.asyncReady++;
            } else {
              call.ready++;
            }
            break;
          case BufferTag:
            // Select a buffer
            MPI_Mrecv(&bufferId, 1, MPI_UNSIGNED, &message, MPI_STATUS_IGNORE);

            // Probe buffer receive from some source/tag
            probe(status.MPI_SOURCE, status.MPI_TAG, message, status);

            // Get the size that is received
            MPI_Get_count(&status, MPI_BYTE, &size);

            // Receive the buffer
            buf = m_asyncCalls[id]->getBufferPos(bufferId, status.MPI_SOURCE, size);
            MPI_Mrecv(buf, size, MPI_BYTE, &message, MPI_STATUS_IGNORE);

            if (m_asyncCalls[id]->useAsyncCopy(bufferId)) {
              call.asyncReady++;
            }
            break;
          case FinalizeTag:
            MPI_Mrecv(nullptr, 0, MPI_BYTE, &message, MPI_STATUS_IGNORE);

            finalizes[status.MPI_SOURCE]++;
            call.ready++;
            break;
          case RegisterTag:
          case WaitTag:
            MPI_Mrecv(nullptr, 0, MPI_BYTE, &message, MPI_STATUS_IGNORE);

            call.ready++;
            break;
          default:
            logError() << "ASYNC: Unknown tag" << tag << "received";
          }
        }

        const auto& call = state(id);
        complete =
            static_cast<int>(call.ready) >= m_numComputeRanks ||
            call.asyncReady >= static_cast<unsigned int>(m_numComputeRanks) + bufferChunks(id);
      }

      if (!complete) {
        // the dispatcher has been finalized; see whether that was the last thing to wait for
        continue;
      }

      auto& call = state(id);
      if (tag == BufferTag) {
        assert(call.lastTag == InitTag || call.lastTag == ParamTag);

        tag = call.lastTag;
      }

      switch (tag) {
      case RegisterTag:
        // the other ranks of the group wait for this, so that nothing about the module can reach
        // the loop before the module itself
        MPI_Barrier(m_privateGroupComm);
        break;
      case AddTag:
        MPI_Barrier(m_privateGroupComm);
        m_asyncCalls[id]->addBufferInternal(sync != 0, call.local);
        break;
      case ResizeTag:
        MPI_Barrier(m_privateGroupComm);
        m_asyncCalls[id]->resizeBufferInternal(bufferId, call.local);
        break;
      case RemoveTag:
        MPI_Barrier(m_privateGroupComm);
        m_asyncCalls[id]->removeBufferInternal(bufferId);
        break;
      case InitTag:
        MPI_Barrier(m_privateGroupComm);
        m_asyncCalls[id]->execInitInternal(m_asyncCalls[id]->paramBuffer());
        break;
      case ParamTag:
        if (!m_asyncCalls[id]->useAsyncCopy()) {
          MPI_Barrier(m_privateGroupComm);
        }
        m_asyncCalls[id]->execInternal(m_asyncCalls[id]->paramBuffer());
        break;
      case WaitTag:
        MPI_Barrier(m_privateGroupComm);
        m_asyncCalls[id]->waitInternal();
        // This barrier can probably be called before waitInternal()
        // Just leave it here to be 100% sure that everything is done
        // when the wait returns
        MPI_Barrier(m_privateGroupComm);
        break;
      case FinalizeTag:
        // Forget the async call
        m_asyncCalls[id]->finalizeInternal();
        m_asyncCalls[id] = nullptr;
        finalized++;
        break;
      default:
        logError() << "ASYNC: Unknown tag" << tag;
      }

      call.local = LocalBuffer{};
      completeLocal(id, tag);

      if (static_cast<int>(call.ready) >= m_numComputeRanks) {
        call.ready = 0;
      } else {
        call.asyncReady = 0;
      }
    }

    if (killing) {
      // stop all that is left
      killCalls();
    }
    finishLoop();
  }

  /**
   * Run {@link loop} on a thread of its own, so that this rank can compute
   * while it coordinates the group.
   */
  void startLoopThread() {
    assert(isColocated());

    if (pthread_create(&m_loopThread, nullptr, loopThread, this) != 0) {
      logError() << "ASYNC: Failed to start the executor coordination thread";
    }
    m_loopThreadRunning = true;

    if (m_loopAffinity.has_value()) {
      m_loopAffinity.value().setaffinity_np(m_loopThread);
    }
  }

  void finalize() {
    if (m_finalized) {
      return;
    }

    // A rank that leaves modules behind says so with the kill switch, the last thing it sends; the
    // executor shuts them down once every rank of its group is done
    const bool kill = m_liveCalls > 0;

    if (m_loopThreadRunning) {
      // The loop thread ends once all modules are finalized, or with the
      // kill switch of every rank that leaves modules behind
      LocalRequest request;
      request.tag = ShutdownTag;
      request.kill = kill;
      submitLocal(std::move(request), false);
    } else if (kill && m_isCompute && m_privateGroupComm != MPI_COMM_NULL) {
      MPI_Ssend(nullptr, 0, MPI_BYTE, m_executorRank, KillTag, m_privateGroupComm);
    }

    if (m_loopThreadRunning) {
      pthread_join(m_loopThread, nullptr);
      m_loopThreadRunning = false;
    }

    if (m_nodeComm != MPI_COMM_NULL) {
      MPI_Comm_free(&m_nodeComm);
    }
    if (m_privateGroupComm != MPI_COMM_NULL) {
      MPI_Comm_free(&m_privateGroupComm);
    }
    if (m_groupComm != MPI_COMM_NULL && m_groupComm != MPI_COMM_SELF) {
      MPI_Comm_free(&m_groupComm);
    }
    if (m_commWorld != MPI_COMM_WORLD) {
      MPI_Comm_free(&m_commWorld);
    }

    m_finalized = true;
  }

  private:
  static const long DefaultPollIntervalUs = 1000;

  static auto loopThread(void* scheduler) -> void* {
    reinterpret_cast<MPIScheduler*>(scheduler)->loop();
    return nullptr;
  }

  /**
   * Hand a request to the loop thread of this rank.
   *
   * @param blocking Whether to return only once the loop thread has carried
   *  the request out, which is what the group protocol expects everywhere it
   *  would otherwise synchronize on the group communicator
   */
  void submitLocal(LocalRequest request, bool blocking) {
    std::shared_ptr<bool> done;
    if (blocking) {
      done = std::make_shared<bool>(false);
      request.done = done;
    }

    {
      const std::scoped_lock lock(m_localMutex);
      if (m_loopDone) {
        // the executor side has been shut down already, together with everything on it
        return;
      }
      m_localQueue.push_back(std::move(request));
      m_localPending.fetch_add(1, std::memory_order_release);
    }

    if (!blocking) {
      return;
    }

    std::unique_lock<std::mutex> lock(m_localMutex);
    m_localDone.wait(lock, [this, &done]() { return *done || m_loopDone; });
  }

  /**
   * Shut down everything that is left on the executor side
   */
  void killCalls() {
    m_killing = true;
    for (auto& call : m_asyncCalls) {
      if (call != nullptr) {
        call->finalizeInternal();
        call = nullptr;
      }
    }
  }

  /**
   * Leave the loop: nothing that the compute thread of this rank hands over
   * any more is carried out, and nothing it waits for stays pending
   */
  void finishLoop() {
    completeLocal();
    {
      const std::scoped_lock lock(m_localMutex);
      m_loopDone = true;
      m_localQueue.clear();
      m_localPending.store(0, std::memory_order_release);
    }
    m_localDone.notify_all();
  }

  auto nextLocalRequest(LocalRequest& request) -> bool {
    if (m_localPending.load(std::memory_order_acquire) == 0) {
      return false;
    }

    const std::scoped_lock lock(m_localMutex);

    if (m_localQueue.empty()) {
      return false;
    }

    request = std::move(m_localQueue.front());
    m_localQueue.pop_front();
    m_localPending.fetch_sub(1, std::memory_order_release);
    return true;
  }

  /**
   * Wait for the next thing to do, from either transport.
   *
   * @return True if a request of the local compute thread was taken, false if
   *  an MPI message was matched
   */
  auto pollNext(LocalRequest& request, MPI_Message& message, MPI_Status& status) -> bool {
    Backoff backoff(m_pollIntervalUs);

    while (true) {
      if (nextLocalRequest(request)) {
        return true;
      }

      int flag = 0;
      MPI_Improbe(MPI_ANY_SOURCE, MPI_ANY_TAG, m_privateGroupComm, &flag, &message, &status);

      if (flag != 0) {
        return false;
      }

      backoff.pause();
    }
  }

  /**
   * Release the compute thread of this rank once its request has been carried
   * out.
   *
   * The loop collects the messages of several operations before it acts on
   * one of them, so an outstanding local request only belongs to the
   * operation it was issued for.
   */
  void completeLocal(int id, int tag) {
    if (!m_pendingLocal || m_pendingId != id || m_pendingTag != tag) {
      return;
    }

    completeLocal();
  }

  void completeLocal() {
    if (!m_pendingLocal) {
      return;
    }

    {
      const std::scoped_lock lock(m_localMutex);
      *m_pendingLocal = true;
    }
    m_localDone.notify_all();
    m_pendingLocal.reset();
    m_pendingId = -1;
    m_pendingTag = -1;
  }

  /**
   * Wait for a matching message without blocking inside MPI
   */
  void probe(int source, int tag, MPI_Message& message, MPI_Status& status) {
    Backoff backoff(m_pollIntervalUs);

    int flag = 0;
    while (flag == 0) {
      MPI_Improbe(source, tag, m_privateGroupComm, &flag, &message, &status);

      if (flag == 0) {
        backoff.pause();
      }
    }
  }

  [[nodiscard]] auto privateGroupComm() const -> MPI_Comm { return m_privateGroupComm; }

  [[nodiscard]] auto groupSize() const -> int { return m_groupSize; }

  /**
   * Register a module and assign its id.
   *
   * Before the dispatcher starts the executors, every rank, the executors
   * included, creates the same modules in the same order, so the ids agree by
   * construction. A module that comes later only exists where the application
   * runs; its registration then is a step of the group protocol, which an
   * executor that computes as well carries out on behalf of its own rank.
   */
  auto addScheduled(Scheduled* scheduled) -> int {
    const int id = m_nextId++;

    const int* tagUpperBound = nullptr;
    int flag = 0;
    MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_TAG_UB, static_cast<void*>(&tagUpperBound), &flag);
    if (flag != 0 && (id + 1) * NumTags + NumStaticTags > *tagUpperBound) {
      logError() << "ASYNC: Too many modules for the MPI tags of this implementation:" << id + 1;
    }

    scheduled->allocParamBuffer();

    if (m_isCompute) {
      m_liveCalls++;
    }

    if (!m_dispatched) {
      m_asyncCalls.push_back(scheduled);
      return id;
    }

    if (m_executorExclusive) {
      logError() << "ASYNC: A module was attached after the dispatcher started its executors. Only"
                 << "executors that compute as well can take on such modules; leave"
                 << "EXECUTOR_EXCLUSIVE unset.";
    }

    if (isColocated()) {
      LocalRequest request;
      request.tag = RegisterTag;
      request.id = id;
      request.scheduled = scheduled;

      submitLocal(std::move(request), true);
    } else {
      MPI_Send(nullptr,
               0,
               MPI_BYTE,
               m_executorRank,
               id * NumTags + RegisterTag + NumStaticTags,
               m_privateGroupComm);
      MPI_Barrier(m_privateGroupComm);
    }

    return id;
  }

  void addBuffer(
      int id, unsigned int bufferId, bool sync, const void* origin, size_t size, bool clone) {
    assert(id >= 0);

    if (bufferId >= m_heapBufferIds.size()) {
      assert(bufferId == m_heapBufferIds.size()); // IDs always increment by 1

      m_heapBufferIds.push_back(bufferId);
    }

    if (isColocated()) {
      LocalRequest request;
      request.tag = AddTag;
      request.id = id;
      request.sync = sync;
      request.origin = origin;
      request.size = size;
      request.clone = clone;

      submitLocal(std::move(request), true);
      return;
    }

    int syncInt = static_cast<int>(sync);
    MPI_Send(&syncInt,
             1,
             MPI_INT,
             m_executorRank,
             id * NumTags + AddTag + NumStaticTags,
             m_privateGroupComm);

    MPI_Barrier(m_privateGroupComm);
  }

  void resizeBuffer(int id, unsigned int bufferId, const void* origin, size_t size, bool clone) {
    assert(id >= 0);

    if (isColocated()) {
      LocalRequest request;
      request.tag = ResizeTag;
      request.id = id;
      request.bufferId = bufferId;
      request.origin = origin;
      request.size = size;
      request.clone = clone;

      submitLocal(std::move(request), true);
      return;
    }

    MPI_Send(&bufferId,
             1,
             MPI_UNSIGNED,
             m_executorRank,
             id * NumTags + ResizeTag + NumStaticTags,
             m_privateGroupComm);

    MPI_Barrier(m_privateGroupComm);
  }

  void removeBuffer(int id, unsigned int bufferId) {
    assert(id >= 0);

    if (isColocated()) {
      LocalRequest request;
      request.tag = RemoveTag;
      request.id = id;
      request.bufferId = bufferId;

      submitLocal(std::move(request), true);
      return;
    }

    MPI_Send(&bufferId,
             1,
             MPI_UNSIGNED,
             m_executorRank,
             id * NumTags + RemoveTag + NumStaticTags,
             m_privateGroupComm);

    MPI_Barrier(m_privateGroupComm);
  }

  void sendBuffer(int id, unsigned int bufferId, const void* buffer, int size) {
    assert(id >= 0);

    // Select the buffer
    MPI_Send(&bufferId,
             1,
             MPI_UNSIGNED,
             m_executorRank,
             id * NumTags + BufferTag + NumStaticTags,
             m_privateGroupComm);

    // Send the buffer (synchronous to avoid overtaking of other messages)
    MPI_Ssend(buffer,
              size,
              MPI_BYTE,
              m_executorRank,
              id * NumTags + BufferTag + NumStaticTags,
              m_privateGroupComm);
  }

  auto iSendBuffer(int id, unsigned int bufferId, const void* buffer, int size) -> MPIRequest2 {
    assert(id >= 0);

    MPIRequest2 requests{};

    // Select the buffer
    MPI_Isend(&m_heapBufferIds[bufferId],
              1,
              MPI_UNSIGNED,
              m_executorRank,
              id * NumTags + BufferTag + NumStaticTags,
              m_privateGroupComm,
              requests.r.data());

    // Send the buffer
    MPI_Isend(buffer,
              size,
              MPI_BYTE,
              m_executorRank,
              id * NumTags + BufferTag + NumStaticTags,
              m_privateGroupComm,
              &requests.r[1]);

    return requests;
  }

  template <typename Parameter>
  void sendInitParam(int id, const Parameter& param) {
    assert(id >= 0);

    if (isColocated()) {
      submitLocal(paramRequest(InitTag, id, param), true);
      return;
    }

    // For simplification, we send the Parameter struct as a buffer
    // TODO find a nice way for hybrid systems
    MPI_Send(const_cast<Parameter*>(&param),
             sizeof(Parameter),
             MPI_BYTE,
             m_executorRank,
             id * NumTags + InitTag + NumStaticTags,
             m_privateGroupComm);

    MPI_Barrier(m_privateGroupComm);
  }

  /**
   * Send message to the executor task
   */
  template <typename Parameter>
  void sendParam(int id, const Parameter& param) {
    assert(id >= 0);

    if (isColocated()) {
      submitLocal(paramRequest(ParamTag, id, param), true);
      return;
    }

    // For simplification, we send the Parameter struct as a buffer
    // TODO find a nice way for hybrid systems
    MPI_Send(const_cast<Parameter*>(&param),
             sizeof(Parameter),
             MPI_BYTE,
             m_executorRank,
             id * NumTags + ParamTag + NumStaticTags,
             m_privateGroupComm);

    MPI_Barrier(m_privateGroupComm);
  }

  /**
   * Send message asynchronous to the executor task
   */
  template <typename Parameter>
  auto iSendParam(int id, const Parameter& param) -> MPI_Request {
    assert(id >= 0);

    MPI_Request request = MPI_REQUEST_NULL;

    if (isColocated()) {
      // Not waiting here is the whole point of the asynchronous copy mode
      submitLocal(paramRequest(ParamTag, id, param), false);
      return request;
    }

    MPI_Isend(const_cast<Parameter*>(&param),
              sizeof(Parameter),
              MPI_BYTE,
              m_executorRank,
              id * NumTags + ParamTag + NumStaticTags,
              m_privateGroupComm,
              &request);

    // suppress no matching receive linter issues
    // NOLINTNEXTLINE
    return request;
  }

  void wait(int id) {
    assert(id >= 0);

    if (isColocated()) {
      LocalRequest request;
      request.tag = WaitTag;
      request.id = id;

      submitLocal(std::move(request), true);
      return;
    }

    MPI_Send(nullptr,
             0,
             MPI_BYTE,
             m_executorRank,
             id * NumTags + WaitTag + NumStaticTags,
             m_privateGroupComm);

    // Wait for the return of the async call (note: need to call the barrier twice)
    MPI_Barrier(m_privateGroupComm);
    MPI_Barrier(m_privateGroupComm);
  }

  void sendFinalize(int id) {
    assert(id >= 0);

    if (m_privateGroupComm == MPI_COMM_NULL) {
      // Can happen if the dispatcher was already finalized
      // We can ignore this here because, we have the kill switch
      return;
    }

    m_liveCalls--;

    if (isColocated()) {
      LocalRequest request;
      request.tag = FinalizeTag;
      request.id = id;

      // The executor side owns the buffers of this rank and finalizes the
      // module once the whole group is done with it; wait for that
      submitLocal(std::move(request), true);
      return;
    }

    MPI_Send(nullptr,
             0,
             MPI_BYTE,
             m_executorRank,
             id * NumTags + FinalizeTag + NumStaticTags,
             m_privateGroupComm);
  }

  template <typename Parameter>
  auto paramRequest(int tag, int id, const Parameter& param) -> LocalRequest {
    LocalRequest request;
    request.tag = tag;
    request.id = id;
    request.param.resize(sizeof(Parameter));
    memcpy(request.param.data(), &param, sizeof(Parameter));

    return request;
  }

  void addManagedBuffer(size_t size) {
    m_managedBufferCounter[size]++;
    if (m_managedBuffer.size() < size) {
      m_managedBuffer.resize(size);
    }
  }

  void resizeManagedBuffer(size_t oldSize, size_t newSize) {
    if (oldSize == newSize) {
      return;
    }

    m_managedBufferCounter[newSize]++;
    m_managedBufferCounter.at(oldSize)--;
    if (m_managedBufferCounter.at(oldSize) == 0) {
      m_managedBufferCounter.erase(oldSize);
    }
    if (m_managedBuffer.size() == oldSize) {
      if (newSize > oldSize) {
        m_managedBuffer.resize(newSize);
      } else {
        const auto it = m_managedBufferCounter.rbegin();
        assert(it != m_managedBufferCounter.rend());
        m_managedBuffer.resize(it->first);
      }
    }
  }

  void removeManagedBuffer(size_t size) {
    m_managedBufferCounter.at(size)--;
    if (m_managedBufferCounter.at(size) == 0) {
      m_managedBufferCounter.erase(size);
      if (m_managedBuffer.size() == size) {
        // Last element removed that required this size
        const auto it = m_managedBufferCounter.rbegin();
        if (it == m_managedBufferCounter.rend()) {
          m_managedBuffer.clear();
        } else {
          m_managedBuffer.resize(it->first);
        }
      }
    }
  }

  /**
   * @return Current pointer to the managed buffer
   */
  auto managedBuffer() -> uint8_t* { return m_managedBuffer.data(); }

  static const int KillTag = 0;
  static const int NumStaticTags = KillTag + 1;

  static const int AddTag = 0;
  static const int ResizeTag = 1;
  static const int RemoveTag = 2;
  static const int InitTag = 3;
  static const int BufferTag = 4;
  static const int ParamTag = 5;
  static const int WaitTag = 6;
  static const int FinalizeTag = 7;
  static const int RegisterTag = 8;
  /** The number of tags required for each async module */
  static const int NumTags = RegisterTag + 1;
  /** Only ever handed over locally: the dispatcher is finalized */
  static const int ShutdownTag = NumTags;
};

} // namespace async::as

#endif // ASYNC_AS_MPISCHEDULER_H
