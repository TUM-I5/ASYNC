// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifndef ASYNC_AS_BASE_H
#define ASYNC_AS_BASE_H

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "Pin.h"
#include "utils/logger.h"

#include "Magic.h"
#include "async/Config.h"
#include "async/ExecInfo.h"
#include "async/NoParam.h"
#include "async/as/Pin.h"

namespace async::as {

class MPIScheduler;

/**
 * Base class for (a)synchronous communication
 */
template <class Executor, typename InitParameter = NoParam, typename Parameter = NoParam>
class Base : public async::ExecInfo {
  private:
  ASYNC_HAS_MEM_FUNC_T1(execInit, execInitHasExec, P, void, const ExecInfo&, const P&);
  ASYNC_HAS_MEM_FUNC_T1(exec, execHasExec, P, void, const ExecInfo&, const P&);
  ASYNC_HAS_MEM_FUNC_T1(execWait, execWaitHasExec, P, void, const ExecInfo&);
  ASYNC_HAS_MEM_FUNC_T1(execWait, execWaitHasNoExec, P, void);

  /**
   * Description of a buffer
   */
  struct BufInfo {
    /** The original memory */
    const void* origin;
    /** The buffer on the executor */
    void* buffer;
  };

  /** The executor for the asynchronous call */
  Executor* m_executor;

  /** The buffers */
  std::vector<BufInfo> m_buffer;

  /** Already cleanup everything? */
  bool m_finalized{false};

  /** Aligment of buffers (might be requested for I/O back-ends) */
  const size_t mAlignment;

  Parameter m_lastParameters;

  protected:
  Base() : m_executor(nullptr), mAlignment(async::Config::alignment()) {}

  public:
  ~Base() override { finalizeInternal(); }

  auto operator=(Base&&) -> Base& = delete;
  auto operator=(const Base&) -> Base& = delete;
  Base(const Base&) = delete;
  Base(Base&&) = delete;

  /**
   * Only required in asynchronous MPI mode
   */
  virtual void setScheduler(MPIScheduler& scheduler) {}

  virtual void setExecutor(Executor& executor) { m_executor = &executor; }

  virtual auto isAffinityNecessary() -> bool { return false; }

  virtual void setAffinityIfNecessary(const as::CpuMask& cpuMask) {
    // Do nothing in the general case.
  }

  /**
   * Add a buffer that is not copied for asychronous calls.
   *
   * Can be used for constant data or for initialization calls.
   *
   * @param clone True of the buffer is the same (a clone) on all MPI processes.
   *  (Allows additional optimizations.)
   */
  virtual auto
      addSyncBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned int = 0;

  /**
   * @param buffer The original memory location in the application or NULL if ASYNC should
   *  manage the buffer. (See {@link managedBuffer()}
   * @param bufferSize The size of the memory location
   * @param clone True of the buffer is the same (a clone) on all MPI processes.
   * @return The id of the buffer
   */
  virtual auto addBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned int = 0;

  /**
   * Resize an existing buffer
   *
   * @param id The id of the buffer
   * @param buffer The new original memory location
   * @param size The new size
   */
  virtual void resizeBuffer(unsigned int id, const void* buffer, size_t size) = 0;

  /**
   * Frees all memory allocated for the buffer.
   *
   * It is errornous to send buffers which have been removed. Removing buffers
   * should only be done between {@link wait()} and {@link call()}.
   *
   * @warning This will not change the results of {@link numBuffers()}.
   */
  virtual void removeBuffer(unsigned int id) {
    assert(id < numBuffers());

    m_buffer[id].origin = nullptr;
    free(m_buffer[id].buffer);
    m_buffer[id].buffer = nullptr;
    async::ExecInfo::removeBufferInternal(id);
  }

  /**
   * @return Pointer to the managed buffer or NULL if this buffer is
   *  not managed
   *
   * @warning This buffer might be shared by ASYNC modules.
   */
  virtual auto managedBuffer(unsigned int id) -> void* {
    if (origin(id) == nullptr) {
      assert(bufferInternal(id) != nullptr || async::ExecInfo::bufferSize(id) == 0);
      return bufferInternal(id);
    }

    return nullptr;
  }

  /**
   * @param size The size that should be transfered
   */
  virtual void sendBuffer(unsigned int id, size_t size) = 0;

  virtual void callInit(const InitParameter& parameters) {
    callInitInternal<Executor, InitParameter>(parameters);
  }

  virtual void call(const Parameter& parameters) { callInternal<Executor, Parameter>(parameters); }

  virtual void wait() { callWaitInternal<Executor, Parameter>(); }

  virtual void finalize() { finalizeInternal(); }

  protected:
  auto executor() -> Executor& { return *m_executor; }

  auto addBufferInternal(const void* origin, size_t size, bool allocate = true) -> unsigned int {
    async::ExecInfo::addBufferInternal(size);

    BufInfo buffer{};
    buffer.origin = origin;

    if ((size != 0U) && allocate) {
      if (mAlignment > 0) {
        const size_t allocBufferSize = allocSize(size);
        const int ret = posix_memalign(&buffer.buffer, mAlignment, allocBufferSize);
        if (ret != 0) {
          logError() << "Could not allocate buffer" << ret;
        }
      } else {
        buffer.buffer = malloc(size);
      }
    } else {
      buffer.buffer = nullptr;
    }

    m_buffer.push_back(buffer);
    assert(m_buffer.size() == numBuffers());

    return m_buffer.size() - 1;
  }

  void resizeBufferInternal(unsigned int id, const void* origin, size_t size) {
    assert(id < numBuffers());
    if (origin && m_buffer[id].origin) {
      m_buffer[id].origin = origin;
    }

    if (async::ExecInfo::bufferSize(id) == size) {
      return;
    }

    async::ExecInfo::resizeBufferInternal(id, size);

    if (size && m_buffer[id].buffer) {
      if (mAlignment > 0) {
        const size_t allocBufferSize = allocSize(size);
        free(m_buffer[id].buffer);
        const int ret = posix_memalign(&m_buffer[id].buffer, mAlignment, allocBufferSize);
        if (ret != 0) {
          logError() << "Could not allocate buffer" << ret;
        }
      } else {
        m_buffer[id].buffer = realloc(m_buffer[id].buffer, size);
      }
    }
  }

  /**
   * Return u_int8_t to allow arithmetic on the pointer
   */
  [[nodiscard]] auto origin(unsigned int id) const -> const uint8_t* {
    assert(id < numBuffers());
    return static_cast<const uint8_t*>(m_buffer[id].origin);
  }

  [[nodiscard]] auto bufferInternal(unsigned int id) const -> const void* {
    assert(id < numBuffers());
    return m_buffer[id].buffer;
  }

  /**
   * Return u_int8_t to allow arithmetic on the pointer
   */
  auto bufferInternal(unsigned int id) -> uint8_t* {
    assert(id < numBuffers());
    return static_cast<uint8_t*>(m_buffer[id].buffer);
  }

  /**
   * Finalize (cleanup) the async call
   *
   * @return False if the class was already finalized
   */
  auto finalizeInternal() -> bool {
    if (m_finalized) {
      return false;
    }

    for (unsigned int i = 0; i < m_buffer.size(); i++) {
      m_buffer[i].origin = nullptr;
      free(m_buffer[i].buffer);
      m_buffer[i].buffer = nullptr;
      async::ExecInfo::removeBufferInternal(i);
    }

    m_finalized = true;
    return true;
  }

  private:
  /**
   * Compute the allocated size depending on the requested size
   */
  auto allocSize(size_t size) -> size_t {
    // Make the allocated buffer size a multiple of m_alignment
    size_t allocBufferSize = (size + mAlignment - 1) / mAlignment;
    allocBufferSize *= mAlignment;

    return allocBufferSize;
  }

  template <typename E, typename P>
  auto callInitInternal(const P& parameters) ->
      typename std::enable_if_t<execInitHasExec<E, P>::Value> {
    const ExecInfo& info = *this;
    m_executor->execInit(info, parameters);
  }

  template <typename E, typename P>
  auto callInitInternal(const P& parameters) ->
      typename std::enable_if_t<!execInitHasExec<E, P>::Value> {
    m_executor->execInit(parameters);
  }

  template <typename E, typename P>
  auto callInternal(const P& parameters) -> typename std::enable_if_t<execHasExec<E, P>::Value> {
    const ExecInfo& info = *this;
    m_executor->exec(info, parameters);
  }

  template <typename E, typename P>
  auto callInternal(const P& parameters) -> typename std::enable_if_t<!execHasExec<E, P>::Value> {
    m_executor->exec(parameters);
  }

  template <typename E, typename P>
  auto callWaitInternal() -> typename std::enable_if_t<execWaitHasExec<E, P>::Value> {
    const ExecInfo& info = *this;
    m_executor->execWait(info);
  }

  template <typename E, typename P>
  auto callWaitInternal() ->
      typename std::enable_if_t<!execWaitHasNoExec<E, P>::Value && !execWaitHasExec<E, P>::Value> {}
};

} // namespace async::as

#endif // ASYNC_AS_BASE_H
