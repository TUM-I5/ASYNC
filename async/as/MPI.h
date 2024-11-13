// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifndef ASYNC_AS_MPI_H
#define ASYNC_AS_MPI_H

#include <cstddef>

#include <cassert>

#include "MPIBase.h"
#include "async/as/Base.h"

namespace async::as {

/**
 * Asynchronous call via MPI
 */
template <class Executor, typename InitParameter, typename Parameter>
class MPI : public MPIBase<Executor, InitParameter, Parameter> {
  public:
  MPI() = default;

  ~MPI() override = default;

  auto addSyncBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned override {
    MPIBase<Executor, InitParameter, Parameter>::addBuffer(buffer, size, clone);
    return Base<Executor, InitParameter, Parameter>::addBufferInternal(buffer, size, false);
  }

  auto addBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned override {
    if (buffer == nullptr) {
      MPIBase<Executor, InitParameter, Parameter>::scheduler().addManagedBuffer(size);
    }

    MPIBase<Executor, InitParameter, Parameter>::addBuffer(buffer, size, clone);
    return Base<Executor, InitParameter, Parameter>::addBufferInternal(buffer, size, false);
  }

  void resizeBuffer(unsigned int id, const void* buffer, size_t size) override {
    if (Base<Executor, InitParameter, Parameter>::origin(id) == nullptr) {
      // Resize the managed buffer
      MPIBase<Executor, InitParameter, Parameter>::scheduler().resizeManagedBuffer(
          Base<Executor, InitParameter, Parameter>::bufferSize(id), size);
    }

    MPIBase<Executor, InitParameter, Parameter>::resizeBuffer(id, buffer, size);
  }

  void removeBuffer(unsigned int id) override {
    if (Base<Executor, InitParameter, Parameter>::origin(id) == nullptr) {
      MPIBase<Executor, InitParameter, Parameter>::scheduler().removeManagedBuffer(
          Base<Executor, InitParameter, Parameter>::bufferSize(id));
    }

    MPIBase<Executor, InitParameter, Parameter>::removeBuffer(id);
  }

  auto managedBuffer(unsigned int id) -> void* override {
    if (Base<Executor, InitParameter, Parameter>::origin(id) == nullptr) {
      return MPIBase<Executor, InitParameter, Parameter>::scheduler().managedBuffer();
    }

    return nullptr;
  }

  [[nodiscard]] auto buffer(unsigned int id) const -> const void* override {
    if (MPIBase<Executor, InitParameter, Parameter>::scheduler().isExecutor()) {
      return MPIBase<Executor, InitParameter, Parameter>::buffer(id);
    }

    return Base<Executor, InitParameter, Parameter>::origin(id);
  }

  /**
   * @warning Only the parameter from one task will be considered
   */
  void call(const Parameter& parameters) override {
    MPIBase<Executor, InitParameter, Parameter>::scheduler().sendParam(
        MPIBase<Executor, InitParameter, Parameter>::id(), parameters);
  }

  private:
  [[nodiscard]] auto useAsyncCopy() const -> bool override { return false; }
};

} // namespace async::as

#endif // ASYNC_AS_MPI_H
