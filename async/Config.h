// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#ifndef ASYNC_CONFIG_H
#define ASYNC_CONFIG_H

#include <cstddef>
#include <string>

#include "utils/env.h"
#include "utils/logger.h"
#include "utils/stringutils.h"

namespace async {

/**
 * The asynchnchronous mode that should be used
 */
enum class Mode { Sync, Thread, MPI };

/**
 * How the compute ranks are grouped around the executors in MPI mode.
 *
 * A group never straddles a node boundary: the aggregation buffer lives in
 * the memory of the executor, "free cores" is a node-local notion, and the
 * rank-to-node mapping is not guaranteed to be block-wise. groupNodes widens
 * a group to several whole nodes for runs that want fewer executors than
 * nodes.
 */
struct GroupLayout {
  /** Number of executors on each node */
  unsigned int executorsPerNode{1};

  /** Number of whole nodes that share one set of executors */
  unsigned int groupNodes{1};

  /** Upper bound on the compute ranks per executor (0: unbounded) */
  unsigned int maxGroupRanks{0};

  /**
   * Whether an executor rank is barred from the computation.
   *
   * An exclusive executor costs a whole rank and splits the application's
   * communicator, which forces every collective in the application onto the
   * reduced communicator. A co-located executor avoids both, at the price of
   * running its coordination and writer threads next to the computation.
   */
  bool exclusive{false};
};

/**
 * What the dispatcher has set up, for the application to report.
 *
 * Outside the MPI mode every rank executes for itself, and only the mode is
 * filled in.
 */
struct ExecutorLayout {
  Mode mode{Mode::Sync};

  /** Number of executors over all nodes */
  int executors{0};

  /** Number of nodes */
  int nodes{0};

  /** Executors on each node, after MAX_GROUP_RANKS has had its say */
  unsigned int executorsPerNode{0};

  /** Whole nodes that share one set of executors */
  unsigned int groupNodes{0};

  /** Whether the executors are barred from the computation */
  bool exclusive{false};

  /** The fewest and the most ranks an executor collects the output of */
  int minGroupRanks{0};
  int maxGroupRanks{0};

  /**
   * Whether the executors, taken in their order, hold the output of the ranks
   * that compute in the order of those ranks: every group is a run of
   * consecutive ranks. Data that one rank numbers for all, like the indices
   * of points in a mesh, only stays valid across the aggregate if this holds.
   */
  bool rankOrdered{true};
};

/**
 * The settings of a single {@link Dispatcher} and of the {@link Module}s
 * attached to it.
 *
 * A Config is a plain value. It reads its defaults from the environment once,
 * at construction; the setters override them afterwards. The environment
 * prefix is supplied by the caller, so an application can namespace the
 * variables:
 *
 * @code
 * async::Config config{utils::Env{"MYAPP_ASYNC_", "ASYNC_"}};
 * @endcode
 */
class Config {
  private:
  Mode m_mode;
  int m_pinCore;
  GroupLayout m_groupLayout;
  bool m_asyncCopy;
  size_t m_alignment;
  size_t m_maxSend;
  long m_pollIntervalUs;
  bool m_sharedMemory;

  public:
  Config() : Config(utils::Env{"ASYNC_"}) {}

  explicit Config(utils::Env env)
      : m_mode(str2mode(env.getOptional<std::string>("MODE").value_or("SYNC"))),
        m_pinCore(env.get<int>("PIN_CORE", -1)), m_groupLayout(readGroupLayout(env)),
        m_asyncCopy(env.get<bool>("MPI_COPY", true)),
        m_alignment(env.get<size_t>("BUFFER_ALIGNMENT", 0)),
        m_maxSend(env.get<size_t>("MPI_MAX_SEND", 1UL << 30)),
        m_pollIntervalUs(env.get<long>("POLL_INTERVAL_US", -1)),
        m_sharedMemory(env.get<bool>("SHARED_MEMORY", true)) {}

  [[nodiscard]] auto mode() const -> Mode { return m_mode; }

  [[nodiscard]] auto pinCore() const -> int { return m_pinCore; }

  [[nodiscard]] auto groupLayout() const -> const GroupLayout& { return m_groupLayout; }

  /**
   * @return The upper bound on the compute ranks per executor (0: unbounded)
   * @deprecated Use {@link groupLayout}
   */
  [[nodiscard]] auto groupSize() const -> unsigned int { return m_groupLayout.maxGroupRanks; }

  [[nodiscard]] auto useAsyncCopy() const -> bool { return m_asyncCopy; }

  [[nodiscard]] auto alignment() const -> size_t { return m_alignment; }

  [[nodiscard]] auto maxSend() const -> size_t { return m_maxSend; }

  /**
   * The longest an idle executor sleeps between two probes, in microseconds.
   * 0 keeps it spinning and yielding instead; a negative value lets the
   * scheduler pick, depending on whether the executor shares its rank with
   * the computation.
   */
  [[nodiscard]] auto pollIntervalUs() const -> long { return m_pollIntervalUs; }

  /**
   * Whether a group whose ranks share memory aggregates in shared memory
   * windows, which every rank writes its part of the output into directly,
   * rather than through messages
   */
  [[nodiscard]] auto sharedMemory() const -> bool { return m_sharedMemory; }

  auto setMode(Mode mode) -> Config& {
    m_mode = mode;
    return *this;
  }

  auto setPinCore(int pinCore) -> Config& {
    m_pinCore = pinCore;
    return *this;
  }

  auto setGroupLayout(const GroupLayout& groupLayout) -> Config& {
    m_groupLayout = groupLayout;
    return *this;
  }

  /**
   * @deprecated Use {@link setGroupLayout}
   */
  auto setGroupSize(unsigned int groupSize) -> Config& {
    m_groupLayout.maxGroupRanks = groupSize;
    return *this;
  }

  auto setUseAsyncCopy(bool useAsyncCopy) -> Config& {
    m_asyncCopy = useAsyncCopy;
    return *this;
  }

  auto setAlignment(size_t alignment) -> Config& {
    m_alignment = alignment;
    return *this;
  }

  auto setMaxSend(size_t maxSend) -> Config& {
    m_maxSend = maxSend;
    return *this;
  }

  auto setPollIntervalUs(long pollIntervalUs) -> Config& {
    m_pollIntervalUs = pollIntervalUs;
    return *this;
  }

  auto setSharedMemory(bool sharedMemory) -> Config& {
    m_sharedMemory = sharedMemory;
    return *this;
  }

  static auto readGroupLayout(utils::Env& env) -> GroupLayout {
    GroupLayout layout{};
    layout.executorsPerNode = env.get<unsigned int>("EXECUTORS_PER_NODE", 1);
    layout.groupNodes = env.get<unsigned int>("GROUP_NODES", 1);
    layout.maxGroupRanks = env.get<unsigned int>("MAX_GROUP_RANKS", 0);
    layout.exclusive = env.get<bool>("EXECUTOR_EXCLUSIVE", false);

    const auto legacy = env.getOptional<unsigned int>("GROUP_SIZE");
    if (legacy.has_value()) {
      logWarning() << "The ASYNC setting GROUP_SIZE is deprecated: groups follow node boundaries,"
                   << "which EXECUTORS_PER_NODE, GROUP_NODES and MAX_GROUP_RANKS shape."
                   << "GROUP_SIZE is read as MAX_GROUP_RANKS.";
      layout.maxGroupRanks = legacy.value();
    }

    return layout;
  }

  /**
   * Parse a mode name: SYNC, THREAD or NODE.
   *
   * NODE is the mode that hands the output of the ranks of a node to an
   * executor there; MPI is a deprecated name for it.
   */
  static auto str2mode(const std::string& mode) -> Mode {
    std::string strMode(mode);
    utils::StringUtils::toUpper(strMode);

    if (strMode == "THREAD") {
      return Mode::Thread;
    }
    if (strMode == "NODE" || strMode == "MPI") {
#ifdef USE_MPI
      if (strMode == "MPI") {
        logWarning() << "The ASYNC mode MPI is deprecated; it is called NODE.";
      }
      return Mode::MPI;
#else  // USE_MPI
      logError() << "The ASYNC mode" << strMode << "requires MPI.";
#endif // USE_MPI
    }
    if (strMode != "SYNC") {
      logWarning() << "Unknown ASYNC mode" << strMode << "- the output is written synchronously.";
    }
    return Mode::Sync;
  }
};

} // namespace async

#endif // ASYNC_CONFIG_H
