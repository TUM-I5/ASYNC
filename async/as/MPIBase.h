// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#ifndef ASYNC_AS_MPIBASE_H
#define ASYNC_AS_MPIBASE_H

#include <algorithm>
#include <cstdint>
#include <mpi.h>

#include <cassert>
#include <cstring>
#include <utility>
#include <vector>

#include "MPIScheduler.h"
#include "ThreadBase.h"
#include "async/Config.h"
#include "async/ExecInfo.h"
#include "async/as/Base.h"
#include "async/as/Pin.h"
#include "utils/logger.h"

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

    /**
     * The memory of this rank. On a co-located executor the buffer slot holds
     * the aggregate of the whole group, so the local contribution is
     * described here instead.
     */
    const void* origin;

    /** Size of the local contribution */
    size_t size;

    /** Storage of the local contribution if ASYNC manages the buffer */
    std::vector<uint8_t> managed;
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

  /** Whether this rank has handed over the finalization of the module */
  bool m_finalizeSent{false};

  /**
   * A buffer of a group that shares memory: the aggregate lives in a window
   * that every rank of the group maps, and every rank writes its own part of
   * it directly
   */
  struct SharedWindow {
    MPI_Win window{MPI_WIN_NULL};
    /** The start of the aggregate, as this rank sees it */
    uint8_t* aggregate{nullptr};
    /** Where the part of this rank starts */
    uint8_t* own{nullptr};
  };

  /** The windows by buffer id, where the group shares memory */
  std::vector<SharedWindow> m_windows;

  public:
  explicit MPIBase(Config config)
      : ThreadBase<Executor, InitParameter, Parameter>(config),
        mMaxSend(Base<Executor, InitParameter, Parameter>::config().maxSend()) {}

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
   * Only an executor runs a thread of its own; on a rank that only computes,
   * pinning it would pin the calling thread instead
   */
  auto isAffinityNecessary() -> bool override {
    return m_scheduler != nullptr && m_scheduler->isExecutor();
  }

  void setAffinityIfNecessary(const CpuMask& cpuSet) override {
    if (isAffinityNecessary()) {
      ThreadBase<Executor, InitParameter, Parameter>::setAffinityIfNecessary(cpuSet);
    }
  }

  /**
   * @param executor
   */
  void setExecutor(Executor& executor) override {
    if (m_scheduler == nullptr) {
      logError() << "ASYNC: The module has no scheduler. A module created after"
                 << "Dispatcher::init() has to be attached to the dispatcher.";
    }

    // Initialization on the executor
    if (m_scheduler->isExecutor()) {
      ThreadBase<Executor, InitParameter, Parameter>::setExecutor(executor);
    }
  }

  void removeBuffer(unsigned int id) override {
    m_scheduler->removeBuffer(m_id, id);

    if (shared() && !m_scheduler->isColocated()) {
      // (on an executor that computes as well, the loop thread frees it for both roles)
      freeWindow(id);
    }

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

    if (shared()) {
      sendBufferShared(id, size);
      return;
    }

    if (m_scheduler->isColocated()) {
      sendBufferLocal(id, size);
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
   * Put the contribution of this rank into its part of the shared window. A
   * buffer ASYNC manages already lives there.
   */
  void sendBufferShared(unsigned int id, size_t size) {
    uint8_t* target = m_windows[id].own + bufferPos(id);
    const auto* source = static_cast<const uint8_t*>(localOrigin(id));
    if (source != nullptr && source + bufferPos(id) != target) {
      async::ExecInfo::bufferOrigin(id).copyFrom(target, source + bufferPos(id), size);
    }
    incBufferPos(id, size);
  }

  /**
   * Move the contribution of a co-located executor rank into the aggregation
   * buffer without going through MPI
   */
  void sendBufferLocal(unsigned int id, size_t size) {
    const auto* buffer = static_cast<const uint8_t*>(localOrigin(id));
    if (buffer == nullptr) {
      buffer = static_cast<const uint8_t*>(localManagedBuffer(id));
    }
    if (buffer == nullptr) {
      buffer = m_scheduler->managedBuffer();
    }

    const int rank = m_scheduler->groupRank();

    for (size_t done = 0; done < size; done += maxSend()) {
      const size_t copy = std::min(maxSend(), size - done);

      void* target = getBufferPos(id, rank, static_cast<int>(copy));
      async::ExecInfo::bufferOrigin(id).copyFrom(target, buffer + bufferPos(id), copy);
      incBufferPos(id, copy);
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
    publishWindows();
    m_scheduler->sendInitParam(m_id, parameters);

    resetBufferPosition();
  }

  void finalize() override {
    if (m_scheduler != nullptr && m_scheduler->isColocated()) {
      // The executor side owns the buffers of this rank. It finalizes the
      // module on the loop thread once the whole group is done with it, and
      // this returns only then.
      if (m_id >= 0 && !m_finalizeSent) {
        m_finalizeSent = true;
        m_scheduler->sendFinalize(m_id);
      }
      Base<Executor, InitParameter, Parameter>::finalizeInternal();
      return;
    }

    if (!Base<Executor, InitParameter, Parameter>::finalizeInternal()) {
      return;
    }

    if (m_scheduler != nullptr && m_id >= 0 && m_scheduler->isCompute()) {
      m_scheduler->sendFinalize(m_id);

      // the executor frees the windows together with this rank once it finalizes the module;
      // without a dispatcher, that will not happen any more
      if (!m_scheduler->finalized()) {
        freeWindows();
      }
    }
  }

  protected:
  [[nodiscard]] auto maxSend() const -> size_t { return mMaxSend; }

  /**
   * @return Whether the buffers of this module live in shared windows
   */
  [[nodiscard]] auto shared() const -> bool {
    return m_scheduler != nullptr && m_scheduler->sharedMemory();
  }

  /**
   * The part of a shared window this rank writes to
   */
  [[nodiscard]] auto sharedOwn(unsigned int id) const -> void* { return m_windows[id].own; }

  /**
   * Make what this rank wrote into the shared windows visible to the group,
   * or what the group wrote visible to this rank; called before the
   * executor runs a call
   */
  void syncWindows() {
    for (const auto& window : m_windows) {
      if (window.window != MPI_WIN_NULL) {
        MPI_Win_sync(window.window);
      }
    }
  }

  /**
   * Hand what this rank wrote into the shared windows on to the executor;
   * called before the parameters of a call are sent, which tell the executor
   * that the part of this rank is complete
   */
  void publishWindows() { syncWindows(); }

  [[nodiscard]] auto id() const -> int { return m_id; }

  auto scheduler() -> MPIScheduler& { return *m_scheduler; }

  [[nodiscard]] auto scheduler() const -> const MPIScheduler& { return *m_scheduler; }

  auto addBuffer(const void* buffer, size_t size, bool clone = false, bool sync = true)
      -> unsigned {
    assert(m_scheduler);
    assert(m_scheduler->isCompute());

    m_scheduler->addBuffer(
        m_id, Base<Executor, InitParameter, Parameter>::numBuffers(), sync, buffer, size, clone);

    if (m_scheduler->isColocated()) {
      // The loop thread has registered the buffer for both roles already
      return Base<Executor, InitParameter, Parameter>::numBuffers() - 1;
    }

    return addBufferInternal(buffer, size, clone, sync);
  }

  void resizeBuffer(unsigned int id, const void* buffer, size_t size) override {
    assert(m_scheduler);
    assert(m_scheduler->isCompute());

    if (m_scheduler->isColocated()) {
      m_scheduler->resizeBuffer(m_id, id, buffer, size, m_buffer[id].clone);
      return;
    }

    // Resize the buffer on the compute node
    Base<Executor, InitParameter, Parameter>::resizeBufferInternal(id, buffer, size);

    m_scheduler->resizeBuffer(m_id, id, buffer, size, m_buffer[id].clone);

    resizeBufferInternal(id, buffer, size);
  }

  /**
   * Register a buffer slot on the compute side, unless the loop thread of a
   * co-located executor rank has already done so
   */
  auto addBufferLocal(const void* buffer, size_t size, bool allocate) -> unsigned int {
    if (m_scheduler->isColocated()) {
      return Base<Executor, InitParameter, Parameter>::numBuffers() - 1;
    }

    return Base<Executor, InitParameter, Parameter>::addBufferInternal(buffer, size, allocate);
  }

  [[nodiscard]] auto isClone(unsigned int id) const -> bool { return m_buffer[id].clone; }

  /**
   * The memory this rank contributes to a buffer
   */
  [[nodiscard]] auto localOrigin(unsigned int id) const -> const void* {
    if (m_scheduler->isColocated()) {
      return m_buffer[id].origin;
    }

    return Base<Executor, InitParameter, Parameter>::origin(id);
  }

  /**
   * The memory this rank contributes to a buffer, as the application sees it
   */
  [[nodiscard]] auto localBuffer(unsigned int id) const -> const void* override {
    if (shared()) {
      return m_windows[id].own;
    }

    if (m_scheduler != nullptr && m_scheduler->isColocated()) {
      if (m_buffer[id].origin != nullptr) {
        return m_buffer[id].origin;
      }
      if (!m_buffer[id].managed.empty()) {
        return m_buffer[id].managed.data();
      }
      return nullptr;
    }

    return buffer(id);
  }

  /**
   * Storage of the local contribution where ASYNC manages the buffer
   */
  auto localManagedBuffer(unsigned int id) -> void* {
    if (m_buffer[id].managed.empty()) {
      return nullptr;
    }

    return m_buffer[id].managed.data();
  }

  /**
   * The size this rank contributes to a buffer.
   *
   * On a co-located executor rank the buffer slot reports the size of the
   * aggregate, which is not what the compute side of the same rank hands to
   * {@link sendBuffer}.
   */
  [[nodiscard]] auto localBufferSize(unsigned int id) const -> size_t override {
    if (m_scheduler->isColocated()) {
      return m_buffer[id].size;
    }

    return async::ExecInfo::bufferSize(id);
  }

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

  void addBufferInternal(bool sync, const LocalBuffer& local) override {
    addBufferInternal(local.origin, local.size, local.clone, sync);
  }

  auto addBufferInternal(const void* origin,
                         unsigned long size,
                         bool clone = false,
                         bool sync = true) -> unsigned {
    // If this buffer is a clone, only the first rank will send it
    if (clone && m_scheduler->groupRank() != 0) {
      size = 0;
    }

    const int executorRank = m_scheduler->groupSize() - 1;
    const int numComputeRanks = m_scheduler->numComputeRanks();

    // Compute buffer size and offsets
    unsigned long* bufferOffsets = nullptr;
    if (m_scheduler->isExecutor()) {
      assert(m_scheduler->isCompute() || origin == nullptr);
      assert(m_scheduler->isCompute() || size == 0);

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

    const unsigned long localSize = size;

    // the id of the buffer this adds
    const auto id =
        static_cast<unsigned int>(Base<Executor, InitParameter, Parameter>::numBuffers());
    if (shared()) {
      allocateWindow(id, localSize);
    }

    if (m_scheduler->isExecutor()) {
      unsigned int bufferChunks = 0;
      size = layoutBuffer(bufferOffsets, sync, bufferChunks);

      m_numBufferChunks += bufferChunks;

      // Create the buffer
      if (shared()) {
        // the window is the aggregate; it is not to be touched by the executor thread, since the
        // ranks of the group may write their parts right away
        Base<Executor, InitParameter, Parameter>::addBufferInternal(nullptr, size, false);
        Base<Executor, InitParameter, Parameter>::setExternalBufferInternal(
            id, m_windows[id].aggregate);
      } else {
        ThreadBase<Executor, InitParameter, Parameter>::addBuffer(nullptr, size);
      }

      ExecutorBufInfo executorBufInfo{};
      executorBufInfo.sync = sync;
      executorBufInfo.offsets = bufferOffsets;
      executorBufInfo.bufferChunks = bufferChunks;

      // Initialize the current position
      executorBufInfo.positions = new size_t[numComputeRanks];
      memset(executorBufInfo.positions, 0, numComputeRanks * sizeof(size_t));

      m_executorBuffer.push_back(executorBufInfo);

      if (m_scheduler->isCompute()) {
        pushComputeBuffer(origin, localSize, clone);
      }
    } else {
      pushComputeBuffer(origin, size, clone);
    }

    return Base<Executor, InitParameter, Parameter>::numBuffers() - 1;
  }

  void pushComputeBuffer(const void* origin, size_t size, bool clone) {
    BufInfo bufInfo{};
    bufInfo.clone = clone;
    bufInfo.position = 0;
    bufInfo.origin = origin;
    bufInfo.size = size;

    // A co-located rank cannot stage into the buffer slot, because that slot
    // holds the aggregate of the whole group
    if (origin == nullptr && useAsyncCopy() && m_scheduler->isColocated() && !shared()) {
      bufInfo.managed.resize(size);
    }

    m_buffer.push_back(std::move(bufInfo));
  }

  void resizeBufferInternal(unsigned int id, const LocalBuffer& local) override {
    resizeBufferInternal(id, local.origin, local.size);
  }

  void resizeBufferInternal(unsigned int id, const void* buffer, size_t size) {
    if (m_scheduler->isCompute()) {
      if (m_buffer[id].clone && m_scheduler->groupRank() != 0) {
        size = 0;
      }
    }

    if (m_scheduler->isColocated()) {
      m_buffer[id].origin = buffer;
      m_buffer[id].size = size;
      if (!shared() && (!m_buffer[id].managed.empty() || (buffer == nullptr && useAsyncCopy()))) {
        m_buffer[id].managed.resize(size);
      }
    }

    const int executorRank = m_scheduler->groupSize() - 1;

    // Compute buffer size and offsets
    unsigned long* bufferOffsets = nullptr;
    if (m_scheduler->isExecutor()) {
      assert(m_scheduler->isCompute() || size == 0);

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

    if (shared()) {
      // a window has a fixed size: the group makes a new one
      freeWindow(id);
      allocateWindow(id, size);
    }

    if (m_scheduler->isExecutor()) {
      unsigned int bufferChunks = 0;
      size = layoutBuffer(bufferOffsets, m_executorBuffer[id].sync, bufferChunks);

      m_numBufferChunks += bufferChunks - m_executorBuffer[id].bufferChunks;

      // Resize the buffer
      if (shared()) {
        Base<Executor, InitParameter, Parameter>::resizeBufferInternal(id, nullptr, size);
        Base<Executor, InitParameter, Parameter>::setExternalBufferInternal(
            id, m_windows[id].aggregate);
      } else {
        ThreadBase<Executor, InitParameter, Parameter>::resizeBuffer(id, nullptr, size);
      }

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

    if (shared()) {
      freeWindow(id);
    }

    Base<Executor, InitParameter, Parameter>::removeBuffer(id);
  }

  auto getBufferPos(unsigned int id, int rank, int size) -> void* override {
    assert(rank < m_scheduler->numComputeRanks());
    assert(bufferOffset(id, rank) + size <=
           (Base<Executor, InitParameter, Parameter>::bufferSize(id)));

    void* buf =
        Base<Executor, InitParameter, Parameter>::bufferInternal(id) + bufferOffset(id, rank);
    m_executorBuffer[id].positions[rank] += size;
    return buf;
  }

  void execInitInternal(const void* paramBuffer) override {
    syncWindows();
    const auto* param = reinterpret_cast<const InitParameter*>(paramBuffer);
    Base<Executor, InitParameter, Parameter>::callInit(*param);

    resetBufferPositionOnExecutor();
  }

  void execInternal(const void* paramBuffer) override {
    syncWindows();
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

    // the writer thread is done with the windows once it has stopped
    ThreadBase<Executor, InitParameter, Parameter>::finalize();

    if (!m_scheduler->killing()) {
      // (the ranks of the group free them together with the executor, which they do not when
      // the executor shuts their modules down on its own)
      freeWindows();
    }
  }

  /**
   * Lay out a buffer on the executor: where the part of every rank starts in
   * the aggregate, and how many chunks of it the loop waits for with a call
   *
   * @param offsets The sizes of the parts by group rank; their offsets on return
   * @param sync Whether the ranks send the buffer synchronously
   * @param bufferChunks The number of chunks
   * @return The size of the aggregate
   */
  auto layoutBuffer(unsigned long* offsets, bool sync, unsigned int& bufferChunks)
      -> unsigned long {
    const int numComputeRanks = m_scheduler->numComputeRanks();

    unsigned long size = 0;
    bufferChunks = 0;
    for (int i = 0; i < numComputeRanks; i++) {
      const unsigned long bufSize = offsets[i];
      offsets[i] = size;
      size += bufSize;

      // The contribution of this rank is copied in place, and a shared window
      // gets its contributions without any message, so neither shows up as a
      // chunk the loop has to wait for
      if (bufSize > 0 && !sync && !shared() && i != m_scheduler->groupRank()) {
        bufferChunks += (bufSize + mMaxSend - 1) / mMaxSend;
      }
    }

    return size;
  }

  /**
   * Allocate the shared window of a buffer, together with the rest of the
   * group, and find this rank's part in it
   *
   * @param localSize The size this rank contributes
   */
  void allocateWindow(unsigned int id, unsigned long localSize) {
    MPI_Comm comm = m_scheduler->privateGroupComm();

    // where the part of this rank starts
    unsigned long offset = 0;
    MPI_Exscan(&localSize, &offset, 1, MPI_UNSIGNED_LONG, MPI_SUM, comm);
    if (m_scheduler->groupRank() == 0) {
      // MPI_Exscan leaves the result undefined on the first rank
      offset = 0;
    }

    // the first rank makes room to align the start of the aggregate, which
    // only the executor needs to be aligned
    const size_t alignment = Base<Executor, InitParameter, Parameter>::alignment();
    const auto request =
        static_cast<MPI_Aint>(localSize + ((m_scheduler->groupRank() == 0) ? alignment : 0));

    SharedWindow window;
    void* start = nullptr;
    MPI_Win_allocate_shared(request, 1, MPI_INFO_NULL, comm, &start, &window.window);

    // the start of the window (the part of the first rank that has any memory), rather than the
    // part of this rank
    MPI_Aint segmentSize = 0;
    int unit = 0;
    MPI_Win_shared_query(window.window, MPI_PROC_NULL, &segmentSize, &unit, &start);
    auto* base = static_cast<uint8_t*>(start);

    unsigned long padding = 0;
    if (m_scheduler->isExecutor() && alignment > 0 && base != nullptr) {
      const auto address = reinterpret_cast<std::uintptr_t>(base);
      padding = (alignment - address % alignment) % alignment;
    }
    MPI_Bcast(&padding, 1, MPI_UNSIGNED_LONG, m_scheduler->groupSize() - 1, comm);

    window.aggregate = base == nullptr ? nullptr : base + padding;
    window.own = window.aggregate == nullptr ? nullptr : window.aggregate + offset;

    // one passive epoch for the life of the window; MPI_Win_sync orders the
    // accesses within it
    MPI_Win_lock_all(MPI_MODE_NOCHECK, window.window);

    if (id >= m_windows.size()) {
      m_windows.resize(id + 1);
    }
    m_windows[id] = window;
  }

  /**
   * Free the shared window of a buffer, together with the rest of the group
   */
  void freeWindow(unsigned int id) {
    if (id >= m_windows.size() || m_windows[id].window == MPI_WIN_NULL) {
      return;
    }

    MPI_Win_unlock_all(m_windows[id].window);
    MPI_Win_free(&m_windows[id].window);
    m_windows[id] = SharedWindow{};
  }

  void freeWindows() {
    for (unsigned int id = 0; id < m_windows.size(); id++) {
      freeWindow(id);
    }
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
        memset(execBuffer.positions, 0, m_scheduler->numComputeRanks() * sizeof(size_t));
      }
    }
  }
};

} // namespace async::as

#endif // ASYNC_AS_MPIBASE_H
