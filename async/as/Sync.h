// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifndef ASYNC_AS_SYNC_H
#define ASYNC_AS_SYNC_H

#include "Base.h"
#include "async/ExecInfo.h"

namespace async::as {

/**
 * Asynchronous call via pthreads
 */
template <class Executor, typename InitParameter, typename Parameter>
class Sync : public Base<Executor, InitParameter, Parameter> {
  public:
  Sync() = default;

  ~Sync() override = default;

  auto addSyncBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned int override {
    return Base<Executor, InitParameter, Parameter>::addBufferInternal(buffer, size, false);
  }

  auto addBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned int override {
    return Base<Executor, InitParameter, Parameter>::addBufferInternal(
        buffer, size, buffer == nullptr);
  }

  void resizeBuffer(unsigned int id, const void* buffer, size_t size) override {
    Base<Executor, InitParameter, Parameter>::resizeBufferInternal(id, buffer, size);
  }

  [[nodiscard]] auto buffer(unsigned int id) const -> const void* override {
    if (Base<Executor, InitParameter, Parameter>::origin(id) &&
        async::ExecInfo::bufferOrigin(id).transparentHost()) {
      return Base<Executor, InitParameter, Parameter>::origin(id);
    }

    return Base<Executor, InitParameter, Parameter>::bufferInternal(id);
  }

  void sendBuffer(unsigned int id, size_t size) override {
    if (Base<Executor, InitParameter, Parameter>::origin(id) &&
        !async::ExecInfo::bufferOrigin(id).transparentHost()) {
      async::ExecInfo::bufferOrigin(id).copyFrom(
          Base<Executor, InitParameter, Parameter>::bufferInternal(id),
          Base<Executor, InitParameter, Parameter>::origin(id),
          async::ExecInfo::bufferSize(id));
    }
  }

  void wait() override {
    // wait for the executor to finish
    Base<Executor, InitParameter, Parameter>::wait();
  }

  void call(const Parameter& parameters) override {
    Base<Executor, InitParameter, Parameter>::call(parameters);
  }
};

} // namespace async::as

#endif // ASYNC_AS_SYNC_H
