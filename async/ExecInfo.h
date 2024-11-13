// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifndef ASYNC_EXECINFO_H
#define ASYNC_EXECINFO_H

#include "async/BufferOrigin.h"
#include <cassert>
#include <cstddef>
#include <vector>

namespace async {

/**
 * Buffer information send to the executor on each exec and execInit call
 */
class ExecInfo {
  private:
  /** The size for all buffers */
  std::vector<size_t> m_bufferSize;

  std::vector<BufferOrigin*> m_bufferOrigin;

  HostBufferOrigin* m_hostBuffer;

  public:
  ExecInfo() : m_hostBuffer(new HostBufferOrigin()) {}

  virtual ~ExecInfo() = default;

  /**
   * @return True, if this is an MPI executor
   */
  [[nodiscard]] virtual auto isExecutor() const -> bool {
    return false; // Default for sync and thread
  }

  [[nodiscard]] auto numBuffers() const -> unsigned int { return m_bufferSize.size(); }

  [[nodiscard]] auto bufferSize(unsigned int id) const -> size_t {
    assert(id < numBuffers());
    return m_bufferSize[id];
  }

  [[nodiscard]] auto bufferOrigin(unsigned int id) const -> BufferOrigin& {
    assert(id < numBuffers());
    return *m_bufferOrigin[id];
  }

  /**
   * @return Read-only pointer to the buffer (Useful for executors.)
   */
  [[nodiscard]] virtual auto buffer(unsigned int id) const -> const void* = 0;

  protected:
  void addBufferInternal(size_t size) {
    m_bufferSize.push_back(size);
    m_bufferOrigin.push_back(m_hostBuffer);
  }

  void resizeBufferInternal(unsigned int id, size_t size) {
    assert(id < numBuffers());
    m_bufferSize[id] = size;
  }

  void removeBufferInternal(unsigned int id) {
    assert(id < numBuffers());
    m_bufferSize[id] = 0;
  }
};

} // namespace async

#endif // ASYNC_EXECINFO_H
