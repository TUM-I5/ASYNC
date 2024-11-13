// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifndef ASYNC_MODULE_H
#define ASYNC_MODULE_H

#include "async/BufferOrigin.h"
#include <memory>
#ifdef USE_MPI
#include <mpi.h>
#endif // USE_MPI

#ifdef USE_MPI
#include "async/as/MPI.h"
#include "async/as/MPIAsync.h"
#endif // USE_MPI
#include "async/as/Pin.h"
#include "async/as/Sync.h"
#include "async/as/Thread.h"

#include "Config.h"
#include "ModuleBase.h"

namespace async {

template <class Executor, typename InitParameter, typename Parameter>
class Module : public ModuleBase {
  private:
  std::unique_ptr<async::as::Base<Executor, InitParameter, Parameter>> m_async;

  public:
  Module() {
    switch (Config::mode()) {
    case SYNC:
      m_async = std::make_unique<async::as::Sync<Executor, InitParameter, Parameter>>();
      break;
    case THREAD:
      m_async = std::make_unique<async::as::Thread<Executor, InitParameter, Parameter>>();
      break;
    case MPI:
#ifdef USE_MPI
      if (Config::useAsyncCopy()) {
        m_async = std::make_unique<async::as::MPIAsync<Executor, InitParameter, Parameter>>();
      } else {
        m_async = std::make_unique<async::as::MPI<Executor, InitParameter, Parameter>>();
      }
#else  // USE_MPI
      logError() << "Asynchronous MPI is not supported.";
#endif // USE_MPI
      break;
    }
  }

  void setExecutor(Executor& executor) { m_async->setExecutor(executor); }

  auto isAffinityNecessary() -> bool { return m_async->isAffinityNecessary(); }

  void setAffinityIfNecessary(const as::CpuMask& cpuMask) {
    m_async->setAffinityIfNecessary(cpuMask);
  }

  void init() { setUp(); }

  auto addSyncBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned int {
    return m_async->addSyncBuffer(buffer, size, clone);
  }

  auto addBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned int {
    return m_async->addBuffer(buffer, size, clone);
  }

  void resizeBuffer(unsigned int id, const void* buffer, size_t size) {
    m_async->resizeBuffer(id, buffer, size);
  }

  void removeBuffer(unsigned int id) { m_async->removeBuffer(id); }

  [[nodiscard]] auto numBuffers() const -> unsigned int { return m_async->numBuffers(); }

  [[nodiscard]] auto bufferSize(unsigned int id) const -> size_t { return m_async->bufferSize(id); }

  [[nodiscard]] auto bufferOrigin(unsigned int id) const -> BufferOrigin& {
    return m_async->bufferOrigin(id);
  }

  template <typename T>
  auto managedBuffer(unsigned int id) -> T {
    return static_cast<T>(m_async->managedBuffer(id));
  }

  [[nodiscard]] auto buffer(unsigned int id) const -> const void* { return m_async->buffer(id); }

  /**
   * Sends the complete buffer
   */
  void sendBuffer(unsigned int id) { sendBuffer(id, bufferSize(id)); }

  void sendBuffer(unsigned int id, size_t size) { m_async->sendBuffer(id, size); }

  void callInit(const InitParameter& param) { m_async->callInit(param); }

  void call(const Parameter& param) { m_async->call(param); }

  void wait() { m_async->wait(); }

  void finalize() {
    m_async->finalize();

    tearDown();
  }

  private:
#ifdef USE_MPI
  void setScheduler(as::MPIScheduler& scheduler) override { m_async->setScheduler(scheduler); }
#endif // USE_MPI
};

} // namespace async

#endif // ASYNC_MODULE_H
