// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifndef ASYNC_AS_THREADBASE_H
#define ASYNC_AS_THREADBASE_H

#include <cassert>
#include <cstring>
#include <optional>
#include <pthread.h>

#include "utils/logger.h"

#include "Base.h"
#include "Pin.h"

namespace async::as {

#ifndef __APPLE__
using SpinlockT = pthread_spinlock_t;
inline void lock_spinlock(SpinlockT* lock) { pthread_spin_lock(lock); }
inline void unlock_spinlock(SpinlockT* lock) { pthread_spin_unlock(lock); }
#else
// Spinlock is a LINUX-only feature
using SpinlockT = pthread_mutex_t;
inline void lock_spinlock(SpinlockT* lock) { pthread_mutex_lock(lock); }
inline void unlock_spinlock(SpinlockT* lock) { pthread_mutex_unlock(lock); }
#endif // __APPLE__

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
  enum Phase {
    /** We are between call() and wait() */
    ExecPhase,
    /** We are between wait() and call() */
    SendPhase
  };

  /** Async thread */
  pthread_t m_asyncThread;

  /** Mutex locked by the writer (caller) */
  SpinlockT m_writerLock{};

  /** Mutex locked by the reader (callee) */
  pthread_mutex_t m_readerLock{};

  /** Parameters for the next call */
  Parameter m_nextParams;

  /** The buffer we need to initialize */
  std::optional<unsigned> m_initBuffer;

  /** The current phase */
  Phase m_phase;

  /** Mutex to wait for the buffer initialization to finish */
  SpinlockT m_initBufferLock{};

  /** Shutdown the thread */
  bool m_shutdown{false};

  bool m_waiting{false};

  protected:
  ThreadBase() : m_asyncThread(pthread_self()), m_phase(ExecPhase) {
#ifndef __APPLE__
    pthread_spin_init(&m_writerLock, PTHREAD_PROCESS_PRIVATE);
    pthread_spin_init(&m_initBufferLock, PTHREAD_PROCESS_PRIVATE);
#else
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_PROCESS_PRIVATE);
    pthread_mutex_init(&m_writerLock, &attr);
    pthread_mutex_init(&m_initBufferLock, &attr);

#endif // __APPLE__
    pthread_mutex_init(&m_readerLock, nullptr);
  }

  public:
  ~ThreadBase() override {
    finalize();

#ifndef __APPLE__
    pthread_spin_destroy(&m_initBufferLock);
    pthread_spin_destroy(&m_writerLock);
#else
    pthread_mutex_destroy(&m_initBufferLock);
    pthread_mutex_destroy(&m_writerLock);
#endif // __APPLE__
    pthread_mutex_destroy(&m_readerLock);
  }

  auto operator=(ThreadBase&&) -> ThreadBase& = delete;
  auto operator=(const ThreadBase&) -> ThreadBase& = delete;
  ThreadBase(const ThreadBase&) = delete;
  ThreadBase(ThreadBase&&) = delete;

  void setExecutor(Executor& executor) override {
    Base<Executor, InitParameter, Parameter>::setExecutor(executor);

    // Lock the reader until data is available
    pthread_mutex_lock(&m_readerLock);

    // Lock writer until the memory is initialized
    lock_spinlock(&m_writerLock);
    // initBuffer is only unlocked when the buffer initialization is done
    lock_spinlock(&m_initBufferLock);

    if (pthread_create(&m_asyncThread, nullptr, asyncThread, this) != 0) {
      logError() << "ASYNC: Failed to start asynchronous thread";
    }
  }

  void getAffinity(CpuMask& cpuMask) { cpuMask.getaffinity_np(m_asyncThread); }

  void setAffinity(const CpuMask& cpuMask) { cpuMask.setaffinity_np(m_asyncThread); }

  auto isAffinityNecessary() -> bool override { return true; }

  void setAffinityIfNecessary(const CpuMask& cpuSet) override { setAffinity(cpuSet); }

  auto addBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned int override {
    const unsigned int id =
        Base<Executor, InitParameter, Parameter>::addBufferInternal(buffer, size);

    // Now, initialize the buffer on the executor thread with zeros
    if (m_phase == ExecPhase) {
      lock_spinlock(&m_writerLock);
    }

    m_initBuffer = id;                   // Mark for buffer fill
    pthread_mutex_unlock(&m_readerLock); // Similar to call() but without setting the parameters

    // Wait for the initialization to finish
    lock_spinlock(&m_initBufferLock);

    if (m_phase != ExecPhase) { // SEND_PHASE
      lock_spinlock(&m_writerLock);
    }
    return id;
  }

  void resizeBuffer(unsigned int id, const void* buffer, size_t size) override {
    Base<Executor, InitParameter, Parameter>::resizeBufferInternal(id, buffer, size);

    assert(m_phase != ExecPhase);

    // Initialize the buffer on the executor thread with zeros
    m_initBuffer = id;                   // Mark for buffer fill
    pthread_mutex_unlock(&m_readerLock); // Similar to call() but without setting the parameters

    // Wait for the initialization to finish
    lock_spinlock(&m_initBufferLock);

    lock_spinlock(&m_writerLock);
  }

  [[nodiscard]] auto buffer(unsigned int id) const -> const void* override {
    return Base<Executor, InitParameter, Parameter>::bufferInternal(id);
  }

  /**
   * Wait for the asynchronous call to finish
   */
  void wait() override {
    // wait for the previous call to finish
    lock_spinlock(&m_writerLock);

    // signal wait
    m_waiting = true;
    pthread_mutex_unlock(&m_readerLock);

    // wait for the wait to finish
    lock_spinlock(&m_writerLock);
    m_phase = SendPhase;
  }

  void call(const Parameter& parameters) override {
    memcpy(&m_nextParams, &parameters, sizeof(Parameter));

    m_phase = ExecPhase;
    pthread_mutex_unlock(&m_readerLock);
  }

  void finalize() override {
    if (!Base<Executor, InitParameter, Parameter>::finalizeInternal()) {
      return;
    }

    // Shutdown the thread
    m_shutdown = true;
    pthread_mutex_unlock(&m_readerLock);
    pthread_join(m_asyncThread, nullptr);
  }

  private:
  void waitInternal() { Base<Executor, InitParameter, Parameter>::wait(); }

  /**
   * Wrapper for the parent class because parent class function cannot be called directly
   */
  void callInternal(const Parameter& parameters) {
    Base<Executor, InitParameter, Parameter>::call(parameters);
  }

  static auto asyncThread(void* c) -> void* {
    auto* async = reinterpret_cast<ThreadBase*>(c);

    // Tell everyone that we are read to go
    unlock_spinlock(&async->m_writerLock);

    while (true) {
      // We assume that this lock happens before any unlock from the main thread
      pthread_mutex_lock(&async->m_readerLock);
      if (async->m_shutdown) {
        break;
      }

      if (async->m_waiting) {
        async->waitInternal();
        async->m_waiting = false;
      } else if (async->m_initBuffer.has_value()) {
        // Touch the memory on this thread
        const unsigned int id = async->m_initBuffer.value();
        memset(async->bufferInternal(id), 0, async->bufferSize(id));
        async->m_initBuffer = {};

        // Done
        unlock_spinlock(&async->m_initBufferLock);
      } else {
        async->callInternal(async->m_nextParams);
      }

      unlock_spinlock(&async->m_writerLock);
    }

    return nullptr;
  }
};

} // namespace async::as

#endif // ASYNC_AS_THREADBASE_H
