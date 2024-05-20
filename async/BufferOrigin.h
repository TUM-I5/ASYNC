/**
 * @file
 *  This file is part of ASYNC
 *
 * @author David Schneller <david.schneller@tum.de>
 *
 * @copyright Copyright (c) 2024, Technische Universitaet Muenchen.
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

#ifndef ASYNC_BUFFERORIGIN_H
#define ASYNC_BUFFERORIGIN_H

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace async {

/**
 * Buffer information send to the executor on each exec and execInit call
 */
class BufferOrigin {
  public:
  // allocates memory in the buffer allocation zone
  virtual void* malloc(size_t size) = 0;

  // frees memory in the buffer allocation zone
  virtual void free(void* ptr) = 0;

  // copies memory from the buffer allocation zone to the host
  virtual void copyFrom(void* dest, void* source, size_t size) = 0;

  // copies memory from the host to the buffer allocation zone
  virtual void copyTo(void* dest, void* source, size_t size) = 0;

  // copies memory from the buffer allocation zone to the buffer allocation zone
  virtual void copyBetween(void* dest, void* source, size_t size) = 0;

  // memory can be accessed on host
  virtual bool transparentHost() = 0;

  // memory can be directly passed to MPI
  virtual bool transparentMPI() = 0;
};

class HostBufferOrigin : public BufferOrigin {
  public:
  void* malloc(size_t size) override { return std::malloc(size); }
  void free(void* ptr) override { std::free(ptr); }
  void copyTo(void* dest, void* source, size_t size) override { std::memcpy(dest, source, size); }
  void copyFrom(void* dest, void* source, size_t size) override { std::memcpy(dest, source, size); }
  void copyBetween(void* dest, void* source, size_t size) override {
    std::memcpy(dest, source, size);
  }
  bool transparentHost() override { return true; }
  bool transparentMPI() override { return true; }
};

} // namespace async

#endif // ASYNC_BUFFERORIGIN_H
