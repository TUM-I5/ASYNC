// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#ifndef ASYNC_DISPATCHER_H
#define ASYNC_DISPATCHER_H

#ifdef USE_MPI
#include "async/as/MPIScheduler.h"
#include <array>
#include <mpi.h>
#endif // USE_ASYNC_MPI

#include "Config.h"
#include "ModuleBase.h"
#include "async/as/Pin.h"
#include "utils/logger.h"

namespace async {

class Dispatcher {
  private:
#ifdef USE_MPI
  async::as::MPIScheduler m_scheduler;

  MPI_Comm m_comm{MPI_COMM_WORLD};

  /** Where the caller draws the node boundaries; the ranks that share memory otherwise */
  MPI_Comm m_nodeComm{MPI_COMM_NULL};

  /** The communicator spanning all ranks that run the executors */
  MPI_Comm m_execComm{MPI_COMM_NULL};
#endif // USE_MPI

  Config m_config;

  GroupLayout m_groupLayout;

  public:
  explicit Dispatcher(Config config = Config{})
      : m_config(config), m_groupLayout(m_config.groupLayout()) {}

  ~Dispatcher() {
    // Delete all modules so we can create a new dispatcher
    // probably only important for testing
    ModuleBase::modules().clear();
  }

  auto operator=(Dispatcher&&) -> Dispatcher& = delete;
  auto operator=(const Dispatcher&) -> Dispatcher& = delete;
  Dispatcher(const Dispatcher&) = delete;
  Dispatcher(Dispatcher&&) = delete;

#ifdef USE_MPI
  void setCommunicator(MPI_Comm comm) { m_comm = comm; }

  /**
   * Draw the node boundaries the groups are formed along: the ranks of the
   * communicator that share a node with this one. Without it, a node is
   * what shares memory.
   *
   * The communicator stays with the caller; it is only used in {@link init}.
   */
  void setNodeCommunicator(MPI_Comm nodeComm) { m_nodeComm = nodeComm; }
#endif

  /**
   * Use this to overwrite the mode set from the environment variables, e.g.
   * to settle AUTO on SYNC where the application leaves no room for the
   * threads of ASYNC. Has to be called before {@link init}.
   */
  void setMode(Mode mode) { m_config.setMode(mode); }

  /**
   * Use this to overwrite the group layout set from the environment variables
   */
  void setGroupLayout(const GroupLayout& groupLayout) { m_groupLayout = groupLayout; }

  /**
   * @param groupSize Upper bound on the compute ranks per executor
   * @deprecated Use {@link setGroupLayout}
   */
  void setGroupSize(unsigned int groupSize) { m_groupLayout.maxGroupRanks = groupSize; }

  /**
   * Initialize the dispatcher
   *
   * This has to be called after {@link setCommunicator} and
   * {@link setGroupSize}
   */
  void init() {
    if (m_config.mode() == Mode::Auto) {
      m_config.setMode(chooseMode());
    }

    const auto& modules = ModuleBase::modules();
    for (const auto& module : modules) {
      module->setMode(m_config.mode());
    }

#ifdef USE_MPI
    // Set the scheduler for all modules
    for (const auto& module : modules) {
      module->setScheduler(m_scheduler);
    }

    if (m_config.mode() == Mode::MPI) {
      int provided = MPI_THREAD_SINGLE;
      MPI_Query_thread(&provided);
      if (provided < MPI_THREAD_MULTIPLE) {
        logError() << "ASYNC: MPI mode drives the scheduler loop and the executor itself on two"
                   << "threads, both of which call MPI. This requires MPI_THREAD_MULTIPLE, but the"
                   << "MPI library was initialized with a lower thread level.";
      }

      // Initialize the scheduler
      m_scheduler.setPollIntervalUs(m_config.pollIntervalUs());
      m_scheduler.setAllowSharedMemory(m_config.sharedMemory());
      if (m_nodeComm != MPI_COMM_NULL) {
        m_scheduler.setCommunicator(m_comm, m_nodeComm, m_groupLayout);
      } else {
        m_scheduler.setCommunicator(m_comm, m_groupLayout);
      }

      // Only the executors run the modules
      MPI_Comm_split(m_comm, m_scheduler.isExecutor() ? 0 : MPI_UNDEFINED, 0, &m_execComm);
      m_scheduler.determineOrder(m_execComm);
    } else {
      // Every rank runs its own executor
      MPI_Comm_dup(m_comm, &m_execComm);
    }

    for (const auto& module : modules) {
      module->setComm(m_execComm);
    }
#endif // USE_MPI
  }

  /**
   * @return The settings this dispatcher and its modules were created with;
   *  after {@link init}, with the mode AUTO has settled on
   */
  [[nodiscard]] auto config() const -> const Config& { return m_config; }

  /**
   * @return The group layout that will be used in MPI mode
   */
  [[nodiscard]] auto groupLayout() const -> const GroupLayout& { return m_groupLayout; }

  /**
   * @return What {@link init} has set up; only the mode outside the MPI mode
   */
  [[nodiscard]] auto layout() const -> ExecutorLayout {
#ifdef USE_MPI
    if (m_config.mode() == Mode::MPI) {
      return m_scheduler.layout();
    }
#endif // USE_MPI
    ExecutorLayout layout;
    layout.mode = m_config.mode();
    return layout;
  }

#ifdef USE_MPI
  [[nodiscard]] auto groupComm() const -> MPI_Comm { return m_scheduler.groupComm(); }

  [[nodiscard]] auto commWorld() const -> MPI_Comm { return m_scheduler.commWorld(); }

  /**
   * @return The communicator spanning all ranks that run the executors.
   *  MPI_COMM_NULL on ranks that never execute.
   */
  [[nodiscard]] auto execComm() const -> MPI_Comm { return m_execComm; }
#endif // USE_MPI

  /**
   * @return True if the process is an MPI executor
   */
  [[nodiscard]] auto isExecutor() const -> bool {
#ifdef USE_MPI
    return m_scheduler.isExecutor();
#else  // USE_MPI
    return false;
#endif // USE_MPI
  }

  /**
   * @return True if the process takes part in the computation
   */
  [[nodiscard]] auto isCompute() const -> bool {
#ifdef USE_MPI
    return m_scheduler.isCompute();
#else  // USE_MPI
    return true;
#endif // USE_MPI
  }

  /**
   * This function will not return for MPI executors until all executors have been
   * finalized. The function has to be called after all async {@link Module}s have
   * been created.
   *
   * @return False if this rank is an MPI executor that does not contribute to
   *  the computation. Co-located executors return true and coordinate their
   *  group on a thread of their own.
   */
  auto dispatch() -> bool {
#ifdef USE_MPI
    if (m_config.mode() == Mode::MPI) {
      m_scheduler.setDispatched();
    }

    if (m_scheduler.isExecutor()) {
      const auto& modules = ModuleBase::modules();
      // Initialize the executor modules
      for (const auto& module : modules) {
        module->setUpOnce();
      }

      if (m_scheduler.isCompute()) {
        // Coordinate the group on a thread of its own and go on computing
        m_scheduler.startLoopThread();
        return true;
      }

      // Run the executor loop
      m_scheduler.loop();

      // Finalize the executor modules
      for (const auto& module : modules) {
        module->tearDown();
      }
      return false;
    }
#endif // USE_MPI

    return true;
  }

  /**
   * Bring a module that was created after {@link init} into the fold; it is
   * set up with its own init() afterwards.
   *
   * After {@link dispatch}, this is a collective operation of the ranks that
   * compute: they have to attach the same modules in the same order. In MPI
   * mode it requires executors that compute as well, since an exclusive
   * executor never creates the module.
   */
  void attach(ModuleBase& module) {
    module.setMode(m_config.mode());
#ifdef USE_MPI
    module.setScheduler(m_scheduler);
    module.setComm(m_execComm);
#endif // USE_MPI
  }

  /**
   * Affinity for the thread that coordinates the group on a co-located
   * executor rank. Has to be set before {@link dispatch}.
   */
  void setAffinity([[maybe_unused]] const as::CpuMask& mask) {
#ifdef USE_MPI
    m_scheduler.setAffinity(mask);
#endif // USE_MPI
  }

  void finalize() {
#ifdef USE_MPI
    m_scheduler.finalize();

    if (m_execComm != MPI_COMM_NULL) {
      MPI_Comm_free(&m_execComm);
      m_execComm = MPI_COMM_NULL;
    }
#endif // USE_MPI
  }

  private:
  /**
   * The mode AUTO settles on: NODE where ranks share a node, every node holds
   * consecutive ranks, and MPI lets several threads call it; THREAD
   * otherwise. With consecutive ranks on every node, the executors hold the
   * output in the order of the ranks, as with a thread on every rank.
   */
  [[nodiscard]] auto chooseMode() const -> Mode {
#ifdef USE_MPI
    MPI_Comm nodeComm = m_nodeComm;
    if (nodeComm == MPI_COMM_NULL) {
      MPI_Comm_split_type(m_comm, MPI_COMM_TYPE_SHARED, 0, MPI_INFO_NULL, &nodeComm);
    }

    int rank = 0;
    int nodeRank = 0;
    int nodeSize = 0;
    MPI_Comm_rank(m_comm, &rank);
    MPI_Comm_rank(nodeComm, &nodeRank);
    MPI_Comm_size(nodeComm, &nodeSize);

    // the ranks of a node are consecutive if every one of them counts from the same first rank
    std::array<int, 2> first{rank - nodeRank, -(rank - nodeRank)};
    MPI_Allreduce(MPI_IN_PLACE, first.data(), 2, MPI_INT, MPI_MIN, nodeComm);
    if (nodeComm != m_nodeComm) {
      MPI_Comm_free(&nodeComm);
    }

    int provided = MPI_THREAD_SINGLE;
    MPI_Query_thread(&provided);

    // what has to hold everywhere, and whether some node has several ranks; all as minima
    std::array<int, 3> criteria{(first[0] == -first[1]) ? 1 : 0,
                                (provided >= MPI_THREAD_MULTIPLE) ? 1 : 0,
                                (nodeSize > 1) ? 0 : 1};
    MPI_Allreduce(MPI_IN_PLACE, criteria.data(), 3, MPI_INT, MPI_MIN, m_comm);
    const bool consecutive = criteria[0] != 0;
    const bool multiple = criteria[1] != 0;
    const bool shared = criteria[2] == 0;

    return (shared && consecutive && multiple) ? Mode::MPI : Mode::Thread;
#else  // USE_MPI
    return Mode::Thread;
#endif // USE_MPI
  }
};

} // namespace async

#endif // ASYNC_DISPATCHER_H
