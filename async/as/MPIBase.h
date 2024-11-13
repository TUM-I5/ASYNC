// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifndef ASYNC_AS_MPIBASE_H
#define ASYNC_AS_MPIBASE_H

#include <mpi.h>

#include <cassert>
#include <cstring>
#include <vector>

#include "MPIScheduler.h"
#include "ThreadBase.h"
#include "async/Config.h"

namespace async::as {

/**
 * Asynchronous call via MPI
 *
 * @warning This class behaves very different depending on executor and non-executor
 *  ranks. Some variables are only available on non-executors while others are only
 *  available on executors.
 */
template <class Executor, typename InitParameter, typename Parameter>
class MPIBase : public ThreadBase<Executor, InitParameter, Parameter>, private Scheduled {
  private:
  struct BufInfo {

    /** True if this a clone buffer */
    bool clone;

    /** Current position of the buffer */
    size_t position;
  };

  /**
   * Buffer description on the executor
   */
  struct ExecutorBufInfo {
    /** True if the buffer uses synchronized sends */
    bool sync;

    /** Offsets for all tasks */
    unsigned long* offsets;

    /** Next writing position for all tasks */
    size_t* positions;

    /** Number of asychronous buffer chunks we receive for this buffer */
    unsigned int bufferChunks;
  };

  /** The max amount that should be transfered in a single MPI send operation */
  const size_t mMaxSend;

  /** The scheduler */
  MPIScheduler* m_scheduler{nullptr};

  /** The identifier for this module call */
  int m_id{-1};

  /** Total number of asynchronous buffer chunks (only on the executor rank) */
  unsigned int m_numBufferChunks{0};

  /** Buffer description on non-executors */
  std::vector<BufInfo> m_buffer;

  /** Buffer description on executors */
  std::vector<ExecutorBufInfo> m_executorBuffer;

  public:
  MPIBase() : mMaxSend(async::Config::maxSend()) {}

  ~MPIBase() override { finalize(); }

  auto operator=(MPIBase&&) -> MPIBase& = delete;
  auto operator=(const MPIBase&) -> MPIBase& = delete;
  MPIBase(const MPIBase&) = delete;
  MPIBase(MPIBase&&) = delete;

  void setScheduler(MPIScheduler& scheduler) override {
    m_scheduler = &scheduler;

    // Add this to the scheduler
    m_id = m_scheduler->addScheduled(this);
  }

  /**
   * @param executor
   */
  void setExecutor(Executor& executor) override {
    // Initialization on the executor
    if (m_scheduler->isExecutor()) {
      ThreadBase<Executor, InitParameter, Parameter>::setExecutor(executor);
    }
  }

  void removeBuffer(unsigned int id) override {
    m_scheduler->removeBuffer(m_id, id);

    Base<Executor, InitParameter, Parameter>::removeBuffer(id);
  }

  [[nodiscard]] auto buffer(unsigned int id) const -> const void* override {
    if (m_scheduler->isExecutor()) {
      return ThreadBase<Executor, InitParameter, Parameter>::buffer(id);
    }

    return nullptr;
  }

  /**
   * @param id The id of the buffer
   */
  void sendBuffer(unsigned int id, size_t size) override {
    if (size == 0) {
      return;
    }

    assert(id < (Base<Executor, InitParameter, Parameter>::numBuffers()));

    if (isClone(id) && m_scheduler->groupRank() != 0) {
      return;
    }

    const uint8_t* buffer = Base<Executor, InitParameter, Parameter>::origin(id);
    if (buffer == nullptr) {
      buffer = m_scheduler->managedBuffer();
    }

    // We need to send the buffer in 1 GB chunks
    for (size_t done = 0; done < size; done += maxSend()) {
      const size_t send = std::min(maxSend(), size - done);

      m_scheduler->sendBuffer(m_id, id, buffer + bufferPos(id), send);
      incBufferPos(id, send);
    }
  }

  /**
   * Wait for an asynchronous call to finish
   */
  void wait() override {
    // Wait for the call to finish
    m_scheduler->wait(m_id);

    resetBufferPosition();
  }

  /**
   * @warning Only the parameter from one task will be considered
   */
  void callInit(const InitParameter& parameters) override {
    m_scheduler->sendInitParam(m_id, parameters);

    resetBufferPosition();
  }

  void finalize() override {
    if (!Base<Executor, InitParameter, Parameter>::finalizeInternal()) {
      return;
    }

    if (m_id >= 0 && !m_scheduler->isExecutor()) {
      m_scheduler->sendFinalize(m_id);
    }
  }

  protected:
  [[nodiscard]] auto maxSend() const -> size_t { return mMaxSend; }

  [[nodiscard]] auto id() const -> int { return m_id; }

  auto scheduler() -> MPIScheduler& { return *m_scheduler; }

  [[nodiscard]] auto scheduler() const -> const MPIScheduler& { return *m_scheduler; }

  auto addBuffer(const void* buffer, size_t size, bool clone = false, bool sync = true)
      -> unsigned {
    assert(m_scheduler);
    assert(!m_scheduler->isExecutor());

    m_scheduler->addBuffer(m_id, Base<Executor, InitParameter, Parameter>::numBuffers());

    return addBufferInternal(buffer, size, clone, sync);
  }

  void resizeBuffer(unsigned int id, const void* buffer, size_t size) override {
    assert(m_scheduler);
    assert(!m_scheduler->isExecutor());

    // Resize the buffer on the compute node
    Base<Executor, InitParameter, Parameter>::resizeBufferInternal(id, buffer, size);

    m_scheduler->resizeBuffer(m_id, id);

    resizeBufferInternal(id, buffer, size);
  }

  [[nodiscard]] auto isClone(unsigned int id) const -> bool { return m_buffer[id].clone; }

  /**
   * The current position of a buffer on non-executors
   */
  [[nodiscard]] auto bufferPos(unsigned int id) const -> size_t { return m_buffer[id].position; }

  /**
   * Increment the current buffer position on a non-executor
   */
  void incBufferPos(unsigned int id, size_t increment) { m_buffer[id].position += increment; }

  private:
  /**
   * Reset the buffer position on non-executors
   */
  void resetBufferPosition() {
    for (auto& buffer : m_buffer) {
      buffer.position = 0;
    }
  }

  [[nodiscard]] auto paramSize() const -> unsigned int override {
    return std::max(sizeof(InitParameter), sizeof(Parameter));
  }

  // (we require an explicit forward declaration here, to prevent errors due to overloading)
  [[nodiscard]] auto useAsyncCopy() const -> bool override = 0;

  [[nodiscard]] auto useAsyncCopy(unsigned int id) const -> bool override {
    assert(id < m_executorBuffer.size());

    return useAsyncCopy() && !m_executorBuffer[id].sync;
  }

  [[nodiscard]] auto numBufferChunks() const -> unsigned int override { return m_numBufferChunks; }

  void addBufferInternal(bool sync) override { addBufferInternal(nullptr, 0, false, sync); }

  auto addBufferInternal(const void* origin,
                         unsigned long size,
                         bool clone = false,
                         bool sync = true) -> unsigned {
    // If this buffer is a clone, only the first rank will send it
    if (clone && m_scheduler->groupRank() != 0) {
      size = 0;
    }

    const int executorRank = m_scheduler->groupSize() - 1;

    // Compute buffer size and offsets
    unsigned long* bufferOffsets = nullptr;
    if (m_scheduler->isExecutor()) {
      assert(origin == nullptr);
      assert(size == 0);

      bufferOffsets = new unsigned long[m_scheduler->groupSize()];
    }

    MPI_Gather(&size,
               1,
               MPI_UNSIGNED_LONG,
               bufferOffsets,
               1,
               MPI_UNSIGNED_LONG,
               executorRank,
               m_scheduler->privateGroupComm());

    if (m_scheduler->isExecutor()) {
      // Compute total size, offsets and buffer chunks
      size = 0;
      unsigned int bufferChunks = 0;
      for (int i = 0; i < m_scheduler->groupSize() - 1; i++) {
        // Compute offsets from the size
        const unsigned long bufSize = bufferOffsets[i];
        bufferOffsets[i] = size;

        if (bufSize > 0) {
          // Increment the total buffer size
          size += bufSize;

          if (!sync) {
            // Increment the number of buffer chunks
            bufferChunks += (bufSize + mMaxSend - 1) / mMaxSend;
          }
        }
      }

      m_numBufferChunks += bufferChunks;

      // Create the buffer
      ThreadBase<Executor, InitParameter, Parameter>::addBuffer(nullptr, size);

      ExecutorBufInfo executorBufInfo{};
      executorBufInfo.sync = sync;
      executorBufInfo.offsets = bufferOffsets;
      executorBufInfo.bufferChunks = bufferChunks;

      // Initialize the current position
      executorBufInfo.positions = new size_t[m_scheduler->groupSize() - 1];
      memset(executorBufInfo.positions, 0, (m_scheduler->groupSize() - 1) * sizeof(size_t));

      m_executorBuffer.push_back(executorBufInfo);
    } else {
      BufInfo bufInfo{};
      bufInfo.clone = clone;
      bufInfo.position = 0;

      m_buffer.push_back(bufInfo);
    }

    return Base<Executor, InitParameter, Parameter>::numBuffers() - 1;
  }

  void resizeBufferInternal(unsigned int id) override { resizeBufferInternal(id, nullptr, 0); }

  void resizeBufferInternal(unsigned int id, const void* buffer, size_t size) {
    if (!m_scheduler->isExecutor()) {
      if (m_buffer[id].clone && m_scheduler->groupRank() != 0) {
        size = 0;
      }
    }

    const int executorRank = m_scheduler->groupSize() - 1;

    // Compute buffer size and offsets
    unsigned long* bufferOffsets = nullptr;
    if (m_scheduler->isExecutor()) {
      assert(size == 0);

      bufferOffsets = m_executorBuffer[id].offsets;
    }

    MPI_Gather(&size,
               1,
               MPI_UNSIGNED_LONG,
               bufferOffsets,
               1,
               MPI_UNSIGNED_LONG,
               executorRank,
               m_scheduler->privateGroupComm());

    if (m_scheduler->isExecutor()) {
      // Compute total size, offsets and buffer chunks
      size = 0;
      unsigned int bufferChunks = 0;
      for (int i = 0; i < m_scheduler->groupSize() - 1; i++) {
        // Compute offsets from the size
        const unsigned long bufSize = bufferOffsets[i];
        bufferOffsets[i] = size;

        if (bufSize > 0) {
          // Increment the total buffer size
          size += bufSize;

          if (!m_executorBuffer[id].sync) {
            // Increment the number of buffer chunks
            bufferChunks += (bufSize + mMaxSend - 1) / mMaxSend;
          }
        }
      }

      m_numBufferChunks += bufferChunks - m_executorBuffer[id].bufferChunks;

      // Resize the buffer
      ThreadBase<Executor, InitParameter, Parameter>::resizeBuffer(id, nullptr, size);

      m_executorBuffer[id].bufferChunks = bufferChunks;
    }
  }

  void removeBufferInternal(unsigned int id) override {
    assert(id < m_executorBuffer.size());

    m_numBufferChunks -= m_executorBuffer[id].bufferChunks;
    m_executorBuffer[id].bufferChunks = 0;

    delete[] m_executorBuffer[id].offsets;
    m_executorBuffer[id].offsets = nullptr;
    delete[] m_executorBuffer[id].positions;
    m_executorBuffer[id].positions = nullptr;

    Base<Executor, InitParameter, Parameter>::removeBuffer(id);
  }

  auto getBufferPos(unsigned int id, int rank, int size) -> void* override {
    assert(rank < m_scheduler->groupSize() - 1);
    assert(bufferOffset(id, rank) + size <=
           (Base<Executor, InitParameter, Parameter>::bufferSize(id)));

    void* buf =
        Base<Executor, InitParameter, Parameter>::bufferInternal(id) + bufferOffset(id, rank);
    m_executorBuffer[id].positions[rank] += size;
    return buf;
  }

  void execInitInternal(const void* paramBuffer) override {
    const auto* param = reinterpret_cast<const InitParameter*>(paramBuffer);
    Base<Executor, InitParameter, Parameter>::callInit(*param);

    resetBufferPositionOnExecutor();
  }

  void execInternal(const void* paramBuffer) override {
    const auto* param = reinterpret_cast<const Parameter*>(paramBuffer);
    ThreadBase<Executor, InitParameter, Parameter>::call(*param);
  }

  void waitInternal() override {
    ThreadBase<Executor, InitParameter, Parameter>::wait();

    resetBufferPositionOnExecutor();
  }

  void finalizeInternal() override {
    for (auto& execBuffer : m_executorBuffer) {
      delete[] execBuffer.offsets;
      execBuffer.offsets = nullptr;
      delete[] execBuffer.positions;
      execBuffer.positions = nullptr;
    }

    ThreadBase<Executor, InitParameter, Parameter>::finalize();
  }

  /**
   * @return The current offset on the buffer on the executor
   */
  [[nodiscard]] auto bufferOffset(unsigned int id, int rank) const -> size_t {
    return m_executorBuffer[id].offsets[rank] + m_executorBuffer[id].positions[rank];
  }

  /**
   * Reset buffer positions
   *
   * Should only be called on the executor.
   */
  void resetBufferPositionOnExecutor() {
    for (auto& execBuffer : m_executorBuffer) {
      if (execBuffer.positions) {
        memset(execBuffer.positions, 0, (m_scheduler->groupSize() - 1) * sizeof(size_t));
      }
    }
  }
};

} // namespace async::as

#endif // ASYNC_AS_MPIBASE_H
