// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#ifndef ASYNC_AS_MPI_H
#define ASYNC_AS_MPI_H

#include <cstddef>

#include <cassert>

#include "MPIBase.h"
#include "async/Config.h"

namespace async::as {

/**
 * Asynchronous call via MPI
 */
template <class Executor, typename InitParameter, typename Parameter>
class MPI : public MPIBase<Executor, InitParameter, Parameter> {
  public:
  explicit MPI(Config config = Config{}) : MPIBase<Executor, InitParameter, Parameter>(config) {}

  ~MPI() override = default;

  auto addSyncBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned override {
    MPIBase<Executor, InitParameter, Parameter>::addBuffer(buffer, size, clone);
    return MPIBase<Executor, InitParameter, Parameter>::addBufferLocal(buffer, size, false);
  }

  auto addBuffer(const void* buffer, size_t size, bool clone = false) -> unsigned override {
    if (buffer == nullptr) {
      MPIBase<Executor, InitParameter, Parameter>::scheduler().addManagedBuffer(size);
    }

    MPIBase<Executor, InitParameter, Parameter>::addBuffer(buffer, size, clone);
    return MPIBase<Executor, InitParameter, Parameter>::addBufferLocal(buffer, size, false);
  }

  void resizeBuffer(unsigned int id, const void* buffer, size_t size) override {
    if (MPIBase<Executor, InitParameter, Parameter>::localOrigin(id) == nullptr) {
      // Resize the managed buffer
      MPIBase<Executor, InitParameter, Parameter>::scheduler().resizeManagedBuffer(
          MPIBase<Executor, InitParameter, Parameter>::localBufferSize(id), size);
    }

    MPIBase<Executor, InitParameter, Parameter>::resizeBuffer(id, buffer, size);
  }

  void removeBuffer(unsigned int id) override {
    if (MPIBase<Executor, InitParameter, Parameter>::localOrigin(id) == nullptr) {
      MPIBase<Executor, InitParameter, Parameter>::scheduler().removeManagedBuffer(
          MPIBase<Executor, InitParameter, Parameter>::localBufferSize(id));
    }

    MPIBase<Executor, InitParameter, Parameter>::removeBuffer(id);
  }

  auto managedBuffer(unsigned int id) -> void* override {
    if (MPIBase<Executor, InitParameter, Parameter>::localOrigin(id) == nullptr) {
      return MPIBase<Executor, InitParameter, Parameter>::scheduler().managedBuffer();
    }

    return nullptr;
  }

  [[nodiscard]] auto buffer(unsigned int id) const -> const void* override {
    if (MPIBase<Executor, InitParameter, Parameter>::scheduler().isExecutor()) {
      return MPIBase<Executor, InitParameter, Parameter>::buffer(id);
    }

    return MPIBase<Executor, InitParameter, Parameter>::localOrigin(id);
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
