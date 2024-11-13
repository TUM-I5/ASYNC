// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifndef ASYNC_DISPATCHER_H
#define ASYNC_DISPATCHER_H

#ifdef USE_MPI
#include <mpi.h>
#endif // USE_MPI

#ifdef USE_ASYNC_MPI
#include "async/as/MPIScheduler.h"
#endif // USE_ASYNC_MPI

#include "Config.h"
#include "ModuleBase.h"

namespace async {

class Dispatcher {
  private:
#ifdef USE_MPI
  async::as::MPIScheduler m_scheduler;

  MPI_Comm m_comm;
#endif // USE_MPI

  unsigned int m_groupSize;

  public:
  Dispatcher()
      :
#ifdef USE_MPI
        m_comm(MPI_COMM_WORLD),
#endif // USE_MPI
        m_groupSize(Config::groupSize()) {
  }

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
   * Use this to overwrite the group size set from the environment variable
   *
   * @param groupSize The group size (excl. the MPI executor)
   * @deprecated Use {@link Config::setGroupSize}
   */
  void setGroupSize(unsigned int groupSize) {
    if (Config::mode() == MPI) {
      m_groupSize = groupSize;
    }
  }

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

    if (Config::mode() == MPI) {
      // Initialize the scheduler
      m_scheduler.setCommunicator(m_comm, m_groupSize);
    }
#endif // USE_MPI
  }

  /**
   * @return The groups size (or 1 for synchronous and asynchnchronous thread mode)
   */
  [[nodiscard]] auto groupSize() const -> unsigned int { return m_groupSize; }

#ifdef USE_MPI
  [[nodiscard]] auto groupComm() const -> MPI_Comm { return m_scheduler.groupComm(); }

  [[nodiscard]] auto commWorld() const -> MPI_Comm { return m_scheduler.commWorld(); }
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
#endif // USE_MPI
  }
};

} // namespace async

#endif // ASYNC_DISPATCHER_H
