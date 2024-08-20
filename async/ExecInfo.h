// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 *
 * @copyright Copyright (c) 2016-2017, Technische Universitaet Muenchen.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *
 *  3. Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ASYNC_EXECINFO_H
#define ASYNC_EXECINFO_H

#include "async/BufferOrigin.h"
#include <cassert>
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
  ExecInfo() { m_hostBuffer = new HostBufferOrigin(); }

  virtual ~ExecInfo() = default;

  /**
   * @return True, if this is an MPI executor
   */
  virtual bool isExecutor() const {
    return false; // Default for sync and thread
  }

  unsigned int numBuffers() const { return m_bufferSize.size(); }

  size_t bufferSize(unsigned int id) const {
    assert(id < numBuffers());
    return m_bufferSize[id];
  }

  BufferOrigin& bufferOrigin(unsigned int id) const {
    assert(id < numBuffers());
    return *m_bufferOrigin[id];
  }

  /**
   * @return Read-only pointer to the buffer (Useful for executors.)
   */
  virtual const void* buffer(unsigned int id) const = 0;

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
