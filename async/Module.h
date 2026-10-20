// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#ifndef ASYNC_MODULE_H
#define ASYNC_MODULE_H

#include "async/BufferOrigin.h"
#include "async/as/Base.h"
#include <cstddef>
#include <memory>

#ifdef USE_MPI
#include "async/as/MPI.h"
#include "async/as/MPIAsync.h"
#include "async/as/MPIScheduler.h"
#include <mpi.h>
#endif // USE_MPI

#include "async/as/Pin.h"
#include "async/as/Sync.h"
#include "async/as/Thread.h"

#include "Config.h"
#include "ModuleBase.h"
#include "utils/logger.h"

namespace async {

template <class Executor, typename InitParameter, typename Parameter>
class Module : public ModuleBase {
  private:
  Config m_config;

  std::unique_ptr<async::as::Base<Executor, InitParameter, Parameter>> m_async;

  public:
  /**
   * A module created with AUTO takes its mode from the dispatcher: in
   * {@link Dispatcher::init}, or in {@link Dispatcher::attach} for a module
   * created afterwards. It cannot be used before.
   */
  explicit Module(Config config = Config{}) : m_config(config) {
    if (m_config.mode() != Mode::Auto) {
      create(m_config.mode());
    }
  }

  void setExecutor(Executor& executor) { impl().setExecutor(executor); }

  auto isAffinityNecessary() -> bool { return impl().isAffinityNecessary(); }

  void setAffinityIfNecessary(const as::CpuMask& cpuMask) {
    impl().setAffinityIfNecessary(cpuMask);
  }

  void init() { setUpOnce(); }

  auto addSyncBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned int {
    return impl().addSyncBuffer(buffer, size, clone);
  }

  auto addBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned int {
    return impl().addBuffer(buffer, size, clone);
  }

  void resizeBuffer(unsigned int id, const void* buffer, size_t size) {
    impl().resizeBuffer(id, buffer, size);
  }

  void removeBuffer(unsigned int id) { impl().removeBuffer(id); }

  [[nodiscard]] auto numBuffers() const -> std::size_t { return impl().numBuffers(); }

  /**
   * The size this rank contributes to a buffer. Inside the executor
   * callbacks, the ExecInfo argument describes what the executor holds.
   */
  [[nodiscard]] auto bufferSize(unsigned int id) const -> size_t {
    return impl().localBufferSize(id);
  }

  [[nodiscard]] auto bufferOrigin(unsigned int id) const -> BufferOrigin& {
    return impl().bufferOrigin(id);
  }

  template <typename T>
  auto managedBuffer(unsigned int id) -> T {
    return static_cast<T>(impl().managedBuffer(id));
  }

  /**
   * The memory this rank contributes to a buffer. Inside the executor
   * callbacks, the ExecInfo argument describes what the executor holds.
   */
  [[nodiscard]] auto buffer(unsigned int id) const -> const void* { return impl().localBuffer(id); }

  /**
   * Sends the complete buffer
   */
  void sendBuffer(unsigned int id) { sendBuffer(id, bufferSize(id)); }

  void sendBuffer(unsigned int id, size_t size) { impl().sendBuffer(id, size); }

  void callInit(const InitParameter& param) { impl().callInit(param); }

  void call(const Parameter& param) { impl().call(param); }

  void wait() { impl().wait(); }

  void finalize() {
    impl().finalize();

    tearDown();
  }

  private:
  void create(Mode mode) {
    m_config.setMode(mode);
    switch (mode) {
    case Mode::Sync:
      m_async = std::make_unique<async::as::Sync<Executor, InitParameter, Parameter>>(m_config);
      break;
    case Mode::Thread:
      m_async = std::make_unique<async::as::Thread<Executor, InitParameter, Parameter>>(m_config);
      break;
    case Mode::MPI:
#ifdef USE_MPI
      if (m_config.useAsyncCopy()) {
        m_async =
            std::make_unique<async::as::MPIAsync<Executor, InitParameter, Parameter>>(m_config);
      } else {
        m_async = std::make_unique<async::as::MPI<Executor, InitParameter, Parameter>>(m_config);
      }
#else  // USE_MPI
      logError() << "Asynchronous MPI is not supported.";
#endif // USE_MPI
      break;
    case Mode::Auto:
      logError() << "ASYNC: AUTO is not a mode a module can run in.";
      break;
    }
  }

  [[nodiscard]] auto impl() const -> async::as::Base<Executor, InitParameter, Parameter>& {
    if (!m_async) {
      logError() << "ASYNC: A module created with the mode AUTO takes its mode from the"
                 << "dispatcher, in Dispatcher::init() or, for a module created afterwards,"
                 << "in Dispatcher::attach().";
    }
    return *m_async;
  }

  void setMode(Mode mode) override {
    if (!m_async) {
      create(mode);
    }
  }

#ifdef USE_MPI
  void setScheduler(as::MPIScheduler& scheduler) override { impl().setScheduler(scheduler); }

  void setComm(MPI_Comm comm) override { impl().setComm(comm); }
#endif // USE_MPI
};

} // namespace async

#endif // ASYNC_MODULE_H
