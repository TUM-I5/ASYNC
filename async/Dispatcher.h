// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#ifndef ASYNC_DISPATCHER_H
#define ASYNC_DISPATCHER_H

#ifdef USE_MPI
#include "async/as/MPIScheduler.h"
#include <mpi.h>
#endif // USE_ASYNC_MPI

#include "Config.h"
#include "ModuleBase.h"
#include "utils/logger.h"

namespace async {

class Dispatcher {
  private:
#ifdef USE_MPI
  async::as::MPIScheduler m_scheduler;

  MPI_Comm m_comm{MPI_COMM_WORLD};

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
#endif

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
#ifdef USE_MPI
    const auto& modules = ModuleBase::modules();
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
      m_scheduler.setCommunicator(m_comm, m_groupLayout);

      // Only the executors run the modules
      MPI_Comm_split(m_comm, m_scheduler.isExecutor() ? 0 : MPI_UNDEFINED, 0, &m_execComm);
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
   * @return The settings this dispatcher and its modules were created with
   */
  [[nodiscard]] auto config() const -> const Config& { return m_config; }

  /**
   * @return The group layout that will be used in MPI mode
   */
  [[nodiscard]] auto groupLayout() const -> const GroupLayout& { return m_groupLayout; }

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
   * This function will not return for MPI executors until all executors have been
   * finalized. The function has to be called after all async {@link Module}s have
   * been created.
   *
   * @return False if this rank is an MPI executor that does not contribute to the
   *  computation.
   */
  auto dispatch() -> bool {
#ifdef USE_MPI
    if (m_scheduler.isExecutor()) {
      const auto& modules = ModuleBase::modules();
      // Initialize the executor modules
      for (const auto& module : modules) {
        module->setUp();
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

  void finalize() {
#ifdef USE_MPI
    m_scheduler.finalize();

    if (m_execComm != MPI_COMM_NULL) {
      MPI_Comm_free(&m_execComm);
      m_execComm = MPI_COMM_NULL;
    }
#endif // USE_MPI
  }
};

} // namespace async

#endif // ASYNC_DISPATCHER_H
