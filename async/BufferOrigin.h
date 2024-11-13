// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author David Schneller <david.schneller@tum.de>
 */

#ifndef ASYNC_BUFFERORIGIN_H
#define ASYNC_BUFFERORIGIN_H

#include <cassert>
#include <cstdlib>
#include <cstring>

namespace async {

/**
 * Buffer information send to the executor on each exec and execInit call
 */
class BufferOrigin {
  public:
  virtual ~BufferOrigin() = default;
  // allocates memory in the buffer allocation zone
  virtual auto malloc(size_t size) -> void* = 0;

  // frees memory in the buffer allocation zone
  virtual void free(void* ptr) = 0;

  // copies memory from the buffer allocation zone to the host
  virtual void copyFrom(void* dest, const void* source, size_t size) = 0;

  // copies memory from the host to the buffer allocation zone
  virtual void copyTo(void* dest, const void* source, size_t size) = 0;

  // copies memory from the buffer allocation zone to the buffer allocation zone
  virtual void copyBetween(void* dest, const void* source, size_t size) = 0;

  // initializes memory on the target device
  virtual void touch(void* ptr, size_t size) = 0;

  // memory can be accessed on host
  virtual auto transparentHost() -> bool = 0;

  // memory can be directly passed to MPI
  virtual auto transparentMPI() -> bool = 0;
};

class HostBufferOrigin : public BufferOrigin {
  public:
  ~HostBufferOrigin() override = default;
  auto malloc(size_t size) -> void* override { return std::malloc(size); }
  void free(void* ptr) override { std::free(ptr); }
  void copyTo(void* dest, const void* source, size_t size) override {
    std::memcpy(dest, source, size);
  }
  void copyFrom(void* dest, const void* source, size_t size) override {
    std::memcpy(dest, source, size);
  }
  void copyBetween(void* dest, const void* source, size_t size) override {
    std::memcpy(dest, source, size);
  }
  void touch(void* ptr, size_t size) override { std::memset(ptr, 0, size); }
  auto transparentHost() -> bool override { return true; }
  auto transparentMPI() -> bool override { return true; }
};

} // namespace async

#endif // ASYNC_BUFFERORIGIN_H
