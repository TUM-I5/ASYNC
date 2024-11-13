// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifndef ASYNC_AS_MPIASYNC_H
#define ASYNC_AS_MPIASYNC_H

#include <cstddef>
#include <mpi.h>

#include <cassert>
#include <vector>

#include "MPIBase.h"
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

    /**
     * The number of asynchronous request required for this buffer.
     *
     * This is not counting the selecting isend.
     */
    unsigned int requests;
  };

  /** Buffer for the parameter */
  Parameter m_paramBuffer;

  /** Buffer description */
  std::vector<BufInfo> m_buffer;

  /** List of MPI requests */
  std::vector<MPI_Request> m_asyncRequests;

  public:
  MPIAsync() {
    // One request always required for the parameters
    m_asyncRequests.push_back(MPI_REQUEST_NULL);
  }

  ~MPIAsync() override = default;

  auto addSyncBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned override {
    MPIBase<Executor, InitParameter, Parameter>::addBuffer(buffer, size, clone);
    const unsigned int id =
        Base<Executor, InitParameter, Parameter>::addBufferInternal(buffer, size, false);

    // We directly send sync buffers
    BufInfo bufInfo{};
    bufInfo.sync = true;
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
    const unsigned int id =
        Base<Executor, InitParameter, Parameter>::addBufferInternal(buffer, size);

    // Initialize the requests
    unsigned int requests = 0;
    if (size > 0 &&
        (!clone || MPIBase<Executor, InitParameter, Parameter>::scheduler().groupRank() == 0)) {
      requests = (size + MPIBase<Executor, InitParameter, Parameter>::maxSend() - 1) /
                 MPIBase<Executor, InitParameter, Parameter>::maxSend();
      m_asyncRequests.insert(m_asyncRequests.end(), requests * 2, MPI_REQUEST_NULL);
    }

    BufInfo bufInfo{};
    bufInfo.sync = false;
    bufInfo.requests = requests;
    m_buffer.push_back(bufInfo);

    assert(m_buffer.size() == (Base<Executor, InitParameter, Parameter>::numBuffers()));

    return id;
  }

  void resizeBuffer(unsigned int id, const void* buffer, size_t size) override {
    assert(id < m_buffer.size());

    int requests = m_buffer[id].requests;
    if (requests > 0) {
      m_buffer[id].requests = (size + MPIBase<Executor, InitParameter, Parameter>::maxSend() - 1) /
                              MPIBase<Executor, InitParameter, Parameter>::maxSend();

      requests = m_buffer[id].requests - requests;
    }

    if (requests > 0) {
      m_asyncRequests.insert(m_asyncRequests.end(), requests * 2, MPI_REQUEST_NULL);
    } else if (requests < 0) {
      m_asyncRequests.erase(m_asyncRequests.end() + requests * 2, m_asyncRequests.end());
    }

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

    if (m_buffer[id].sync) {
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

    // Send parameters
    m_paramBuffer = parameters;
    MPIBase<Executor, InitParameter, Parameter>::scheduler().iSendParam(
        MPIBase<Executor, InitParameter, Parameter>::id(), m_paramBuffer);
  }

  private:
  [[nodiscard]] auto useAsyncCopy() const -> bool override { return true; }

  /**
   * Sends all buffers asynchronously
   *
   * Should only be used in asynchronous copy mode
   */
  void iSendAllBuffers() {
    unsigned int nextRequest = 0;

    // Send all buffers
    for (unsigned int i = 0; i < Base<Executor, InitParameter, Parameter>::numBuffers(); i++) {
      size_t done = 0;
      for (unsigned int j = 0; j < m_buffer[i].requests; j++) {
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

    assert(nextRequest == m_asyncRequests.size() - 1);
  }
};

} // namespace async::as

#endif // ASYNC_AS_MPIASYNC_H
