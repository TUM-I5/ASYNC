// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#ifndef ASYNC_AS_THREADBASE_H
#define ASYNC_AS_THREADBASE_H

#include <cassert>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <pthread.h>

#include "utils/logger.h"

#include "Base.h"
#include "Pin.h"
#include "async/Config.h"

namespace async::as {

/**
 * A binary semaphore: one thread releases it, another one acquires it.
 *
 * The hand-over between the caller and the executor thread needs exactly
 * that. A mutex does not do: only the thread that locked it may unlock it.
 */
class BinarySemaphore {
  private:
  std::mutex m_mutex;
  std::condition_variable m_condition;
  bool m_available;

  public:
  explicit BinarySemaphore(bool available) : m_available(available) {}

  void acquire() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_condition.wait(lock, [this]() { return m_available; });
    m_available = false;
  }

  void release() {
    {
      const std::scoped_lock lock(m_mutex);
      m_available = true;
    }
    m_condition.notify_one();
  }
};

/**
 * Base class for asynchronous calls via pthreads.
 *
 * This class is used by {@link Thread} and
 * {@link MPI}.
 */
template <class Executor, typename InitParameter, typename Parameter>
class ThreadBase : public Base<Executor, InitParameter, Parameter> {
  private:
  /**
   * The possible phases, indicated whether we
   * are betwenn wait() and call() or between
   * call() and wait()
   */
  enum class Phase {
    /** We are between call() and wait() */
    Exec,
    /** We are between wait() and call() */
    Send
  };

  /** Async thread */
  pthread_t m_asyncThread;

  /** Available while the executor thread is idle; the caller holds it between wait() and call() */
  BinarySemaphore m_idle{false};

  /** Available while there is work for the executor thread */
  BinarySemaphore m_work{false};

  /** Parameters for the next call */
  Parameter m_nextParams;

  /** The buffer we need to initialize */
  std::optional<unsigned> m_initBuffer;

  /** The current phase */
  Phase m_phase{Phase::Exec};

  /** Available once the executor thread has initialized a buffer */
  BinarySemaphore m_initBufferDone{false};

  /** Shutdown the thread */
  bool m_shutdown{false};

  /** Whether the thread runs, i.e. the executor has been set */
  bool m_started{false};

  bool m_waiting{false};

  protected:
  explicit ThreadBase(Config config)
      : Base<Executor, InitParameter, Parameter>(config), m_asyncThread(pthread_self()) {}

  public:
  ~ThreadBase() override { finalize(); }

  auto operator=(ThreadBase&&) -> ThreadBase& = delete;
  auto operator=(const ThreadBase&) -> ThreadBase& = delete;
  ThreadBase(const ThreadBase&) = delete;
  ThreadBase(ThreadBase&&) = delete;

  void setExecutor(Executor& executor) override {
    Base<Executor, InitParameter, Parameter>::setExecutor(executor);

    // the thread reports itself idle once it runs
    if (pthread_create(&m_asyncThread, nullptr, asyncThread, this) != 0) {
      logError() << "ASYNC: Failed to start asynchronous thread";
    }
    m_started = true;
  }

  void getAffinity(CpuMask& cpuMask) { cpuMask.getaffinity_np(m_asyncThread); }

  /**
   * Pin the executor thread. Before the executor is set there is no such
   * thread, and the call does nothing.
   */
  void setAffinity(const CpuMask& cpuMask) {
    if (m_started) {
      cpuMask.setaffinity_np(m_asyncThread);
    }
  }

  auto isAffinityNecessary() -> bool override { return true; }

  void setAffinityIfNecessary(const CpuMask& cpuSet) override { setAffinity(cpuSet); }

  auto addBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned int override {
    const unsigned int id =
        Base<Executor, InitParameter, Parameter>::addBufferInternal(buffer, size);

    if (!m_started) {
      // no executor thread yet, which could touch the memory first
      initBuffer(id);
      return id;
    }

    // Now, initialize the buffer on the executor thread with zeros
    if (m_phase == Phase::Exec) {
      m_idle.acquire();
    }

    m_initBuffer = id; // Mark for buffer fill
    m_work.release();  // Similar to call() but without setting the parameters

    // Wait for the initialization to finish
    m_initBufferDone.acquire();

    if (m_phase != Phase::Exec) { // SEND_PHASE
      m_idle.acquire();
    }
    return id;
  }

  void resizeBuffer(unsigned int id, const void* buffer, size_t size) override {
    Base<Executor, InitParameter, Parameter>::resizeBufferInternal(id, buffer, size);

    if (!m_started) {
      initBuffer(id);
      return;
    }

    assert(m_phase != Phase::Exec);

    // Initialize the buffer on the executor thread with zeros
    m_initBuffer = id; // Mark for buffer fill
    m_work.release();  // Similar to call() but without setting the parameters

    // Wait for the initialization to finish
    m_initBufferDone.acquire();

    m_idle.acquire();
  }

  [[nodiscard]] auto buffer(unsigned int id) const -> const void* override {
    return Base<Executor, InitParameter, Parameter>::bufferInternal(id);
  }

  /**
   * Wait for the asynchronous call to finish
   */
  void wait() override {
    // wait for the previous call to finish
    m_idle.acquire();

    // signal wait
    m_waiting = true;
    m_work.release();

    // wait for the wait to finish
    m_idle.acquire();
    m_phase = Phase::Send;
  }

  void call(const Parameter& parameters) override {
    memcpy(&m_nextParams, &parameters, sizeof(Parameter));

    m_phase = Phase::Exec;
    m_work.release();
  }

  void finalize() override {
    if (!Base<Executor, InitParameter, Parameter>::finalizeInternal()) {
      return;
    }

    if (!m_started) {
      // a module that was never set up has no thread to stop
      return;
    }

    // Shutdown the thread
    m_shutdown = true;
    m_work.release();
    pthread_join(m_asyncThread, nullptr);
  }

  private:
  void waitInternal() { Base<Executor, InitParameter, Parameter>::wait(); }

  /**
   * Fill a buffer with zeros, which also places its pages near the thread
   * that does it
   */
  void initBuffer(unsigned int id) {
    if (Base<Executor, InitParameter, Parameter>::bufferSize(id) > 0) {
      memset(Base<Executor, InitParameter, Parameter>::bufferInternal(id),
             0,
             Base<Executor, InitParameter, Parameter>::bufferSize(id));
    }
  }

  /**
   * Wrapper for the parent class because parent class function cannot be called directly
   */
  void callInternal(const Parameter& parameters) {
    Base<Executor, InitParameter, Parameter>::call(parameters);
  }

  static auto asyncThread(void* c) -> void* {
    auto* async = reinterpret_cast<ThreadBase*>(c);

    // Tell everyone that we are read to go
    async->m_idle.release();

    while (true) {
      async->m_work.acquire();
      if (async->m_shutdown) {
        break;
      }

      if (async->m_waiting) {
        async->waitInternal();
        async->m_waiting = false;
      } else if (async->m_initBuffer.has_value()) {
        // Touch the memory on this thread
        async->initBuffer(async->m_initBuffer.value());
        async->m_initBuffer = {};

        // Done
        async->m_initBufferDone.release();
      } else {
        async->callInternal(async->m_nextParams);
      }

      async->m_idle.release();
    }

    return nullptr;
  }
};

} // namespace async::as

#endif // ASYNC_AS_THREADBASE_H
