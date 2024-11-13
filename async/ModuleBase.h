// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifndef ASYNC_MODULEBASE_H
#define ASYNC_MODULEBASE_H

#include <vector>

#ifdef USE_MPI
#include "async/as/MPIScheduler.h"
#endif // USE_MPI

namespace async {

class Dispatcher;

/**
 * Base class for asynchronous modules. Works closely together
 * with the {@link Dispatcher}.
 */
class ModuleBase {
  friend class Dispatcher;

  protected:
  ModuleBase() { modules().push_back(this); }

  public:
  virtual ~ModuleBase() = default;

  // avoid copy and move operations, due to the constructor above

  ModuleBase(const ModuleBase&) = delete;
  ModuleBase(ModuleBase&&) = delete;

  auto operator=(const ModuleBase&) -> ModuleBase& = delete;
  auto operator=(ModuleBase&&) -> ModuleBase& = delete;

  /**
   * Called at initialization. Is also called by the {@link Dispatcher}
   * on MPI executors.
   *
   * Should at least call {@link setExecutor}(*this).
   */
  virtual void setUp() = 0;

  /**
   * Called after finalization. Is also called on MPI
   * executors.
   */
  virtual void tearDown() {}

  private:
  /**
   * List of all I/O modules (required by the dispatcher)
   */
  static auto modules() -> std::vector<ModuleBase*>& {
    // Use a function here to avoid an additional .cpp file
    static std::vector<ModuleBase*> moduleList;
    return moduleList;
  }

#ifdef USE_MPI
  /**
   * Set the scheduler for this module.
   */
  virtual void setScheduler(as::MPIScheduler& scheduler) = 0;
#endif // USE_MPI
};

} // namespace async

#endif // ASYNC_MODULEBASE_H
