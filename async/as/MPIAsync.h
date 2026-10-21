// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#ifndef ASYNC_AS_MPIASYNC_H
#define ASYNC_AS_MPIASYNC_H

#include <cstddef>
#include <mpi.h>

#include <cassert>
#include <vector>

#include "MPIBase.h"
#include "async/Config.h"
#include "async/ExecInfo.h"
#include "async/as/Base.h"
#include "async/as/MPIScheduler.h"

namespace async::as {

/**
 * Asynchronous call via MPI
 */
template <class Executor, typename InitParameter, typename Parameter>
class MPIAsync : public MPIBase<Executor, InitParameter, Parameter> {
  private:
  /**
   * Buffer description (on non-executors)
   */
  struct BufInfo {
    /** A sychnronized buffer */
    bool sync;

    /** Only the first rank of the group sends a clone buffer */
    bool clone;

    /**
     * The number of asynchronous request required for this buffer.
     *
     * This is not counting the selecting isend.
     */
    std::size_t requests;
  };

  /** Buffer for the parameter */
  Parameter m_paramBuffer;

  /** Buffer description */
  std::vector<BufInfo> m_buffer;

  /** List of MPI requests */
  std::vector<MPI_Request> m_asyncRequests;

  public:
  explicit MPIAsync(Config config = Config{})
      : MPIBase<Executor, InitParameter, Parameter>(config) {
    // One request always required for the parameters
    m_asyncRequests.push_back(MPI_REQUEST_NULL);
  }

  ~MPIAsync() override = default;

  auto addSyncBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned override {
    MPIBase<Executor, InitParameter, Parameter>::addBuffer(buffer, size, clone);
    const unsigned int id =
        MPIBase<Executor, InitParameter, Parameter>::addBufferLocal(buffer, size, false);
    MPIBase<Executor, InitParameter, Parameter>::keepCloneCopy(id, buffer, size, clone);

    // We directly send sync buffers
    BufInfo bufInfo{};
    bufInfo.sync = true;
    bufInfo.clone = clone;
    bufInfo.requests = 0;
    m_buffer.push_back(bufInfo);

    assert(m_buffer.size() == (Base<Executor, InitParameter, Parameter>::numBuffers()));

    return id;
  }

  /**
   * @param bufferSize Should be 0 on the executor
   */
  auto addBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned override {
    MPIBase<Executor, InitParameter, Parameter>::addBuffer(buffer, size, clone, false);
    // (a buffer in a shared window needs no staging copy: this rank writes its part directly; nor
    // does a copy of a clone that is not sent, which has memory of its own)
    const bool unsent = MPIBase<Executor, InitParameter, Parameter>::unsentClone(buffer, clone);
    const auto id = MPIBase<Executor, InitParameter, Parameter>::addBufferLocal(
        buffer, size, !MPIBase<Executor, InitParameter, Parameter>::shared() && !unsent);
    MPIBase<Executor, InitParameter, Parameter>::keepCloneCopy(id, buffer, size, clone);

    BufInfo bufInfo{};
    bufInfo.sync = false;
    bufInfo.clone = clone;
    bufInfo.requests = requestsFor(bufInfo, size);
    m_asyncRequests.insert(m_asyncRequests.end(), bufInfo.requests * 2, MPI_REQUEST_NULL);
    m_buffer.push_back(bufInfo);

    assert(m_buffer.size() == (Base<Executor, InitParameter, Parameter>::numBuffers()));

    return id;
  }

  void resizeBuffer(unsigned int id, const void* buffer, size_t size) override {
    assert(id < m_buffer.size());

    // (the requests are not tied to a position in the list; only their number matters)
    const auto oldRequests = m_buffer[id].requests;
    const auto newRequests = requestsFor(m_buffer[id], size);
    if (newRequests > oldRequests) {
      m_asyncRequests.insert(
          m_asyncRequests.end(), (newRequests - oldRequests) * 2, MPI_REQUEST_NULL);
    } else {
      m_asyncRequests.erase(m_asyncRequests.end() - (oldRequests - newRequests) * 2,
                            m_asyncRequests.end());
    }
    m_buffer[id].requests = newRequests;

    MPIBase<Executor, InitParameter, Parameter>::resizeBuffer(id, buffer, size);
  }

  void removeBuffer(unsigned int id) override {
    if (!m_buffer[id].sync) {
      m_asyncRequests.erase(m_asyncRequests.end() - m_buffer[id].requests * 2,
                            m_asyncRequests.end());
      m_buffer[id].requests = 0;
    }

    MPIBase<Executor, InitParameter, Parameter>::removeBuffer(id);
  }

  [[nodiscard]] auto buffer(unsigned int id) const -> const void* override {
    if (MPIBase<Executor, InitParameter, Parameter>::scheduler().isExecutor()) {
      return MPIBase<Executor, InitParameter, Parameter>::buffer(id);
    }

    return Base<Executor, InitParameter, Parameter>::bufferInternal(id);
  }

  auto managedBuffer(unsigned int id) -> void* override {
    if (MPIBase<Executor, InitParameter, Parameter>::hasCloneCopy(id)) {
      return MPIBase<Executor, InitParameter, Parameter>::cloneCopy(id);
    }

    if (MPIBase<Executor, InitParameter, Parameter>::shared()) {
      // what the application writes there is in the aggregate already
      return MPIBase<Executor, InitParameter, Parameter>::localOrigin(id) == nullptr
                 ? MPIBase<Executor, InitParameter, Parameter>::sharedOwn(id)
                 : nullptr;
    }

    if (MPIBase<Executor, InitParameter, Parameter>::scheduler().isColocated()) {
      return MPIBase<Executor, InitParameter, Parameter>::localManagedBuffer(id);
    }

    return Base<Executor, InitParameter, Parameter>::managedBuffer(id);
  }

  /**
   * Wait for an asynchronous call to finish
   */
  void wait() override {
    // Wait for all requests first
    MPI_Waitall(m_asyncRequests.size(), m_asyncRequests.data(), MPI_STATUSES_IGNORE);

    // Wait for the call to finish
    MPIBase<Executor, InitParameter, Parameter>::wait();
  }

  /**
   * @param id The id of the buffer
   */
  void sendBuffer(unsigned int id, size_t size) override {
    if (size == 0) {
      return;
    }

    assert(id < (Base<Executor, InitParameter, Parameter>::numBuffers()));

    if (m_buffer[id].sync || MPIBase<Executor, InitParameter, Parameter>::shared() ||
        MPIBase<Executor, InitParameter, Parameter>::scheduler().isColocated()) {
      MPIBase<Executor, InitParameter, Parameter>::sendBuffer(id, size);
      return;
    }

    // Only copy it to the local buffer
    assert((MPIBase<Executor, InitParameter, Parameter>::bufferPos(id)) + size <=
           (Base<Executor, InitParameter, Parameter>::bufferSize(id)));

    if (Base<Executor, InitParameter, Parameter>::origin(id)) {
      async::ExecInfo::bufferOrigin(id).copyFrom(
          Base<Executor, InitParameter, Parameter>::bufferInternal(id) +
              MPIBase<Executor, InitParameter, Parameter>::bufferPos(id),
          Base<Executor, InitParameter, Parameter>::origin(id) +
              MPIBase<Executor, InitParameter, Parameter>::bufferPos(id),
          size);
    }
    MPIBase<Executor, InitParameter, Parameter>::incBufferPos(id, size);
  }

  /**
   * @warning Only the parameter from the last task will be considered
   */
  void callInit(const InitParameter& parameters) override {
    iSendAllBuffers();

    MPIBase<Executor, InitParameter, Parameter>::callInit(parameters);
  }

  /**
   * @warning Only the parameter from the last task will be considered
   */
  void call(const Parameter& parameters) override {
    iSendAllBuffers();

    MPIBase<Executor, InitParameter, Parameter>::publishWindows();

    // Send parameters
    m_paramBuffer = parameters;
    m_asyncRequests[0] = MPIBase<Executor, InitParameter, Parameter>::scheduler().iSendParam(
        MPIBase<Executor, InitParameter, Parameter>::id(), m_paramBuffer);
  }

  private:
  [[nodiscard]] auto useAsyncCopy() const -> bool override { return true; }

  /**
   * The number of chunks this rank sends for a buffer of the given size
   */
  [[nodiscard]] auto requestsFor(const BufInfo& bufInfo, size_t size) const -> std::size_t {
    // a co-located executor rank copies its own contribution into the aggregation buffer, so it
    // never sends one
    if (bufInfo.sync || size == 0 || MPIBase<Executor, InitParameter, Parameter>::shared() ||
        MPIBase<Executor, InitParameter, Parameter>::scheduler().isColocated() ||
        (bufInfo.clone &&
         MPIBase<Executor, InitParameter, Parameter>::scheduler().groupRank() != 0)) {
      return 0;
    }

    return (size + MPIBase<Executor, InitParameter, Parameter>::maxSend() - 1) /
           MPIBase<Executor, InitParameter, Parameter>::maxSend();
  }

  /**
   * Sends all buffers asynchronously
   *
   * Should only be used in asynchronous copy mode
   */
  void iSendAllBuffers() {
    // request 0 is for the parameters
    std::size_t nextRequest = 1;

    // Send all buffers
    for (std::size_t i = 0; i < Base<Executor, InitParameter, Parameter>::numBuffers(); i++) {
      size_t done = 0;
      for (std::size_t j = 0; j < m_buffer[i].requests; j++) {
        const size_t send =
            std::min(MPIBase<Executor, InitParameter, Parameter>::maxSend(),
                     MPIBase<Executor, InitParameter, Parameter>::bufferPos(i) - done);
        const MPIRequest2 requests =
            MPIBase<Executor, InitParameter, Parameter>::scheduler().iSendBuffer(
                MPIBase<Executor, InitParameter, Parameter>::id(),
                i,
                Base<Executor, InitParameter, Parameter>::bufferInternal(i) + done,
                send);
        done += send;

        m_asyncRequests[nextRequest] = requests.r[0];
        m_asyncRequests[nextRequest + 1] = requests.r[1];
        nextRequest += 2;
      }
    }

    assert(nextRequest == m_asyncRequests.size());
  }
};

} // namespace async::as

#endif // ASYNC_AS_MPIASYNC_H
