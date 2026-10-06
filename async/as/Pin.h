// SPDX-FileCopyrightText: 2023 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ASYNC_AS_PIN_H
#define ASYNC_AS_PIN_H

#include <pthread.h>

// affinity masks are a Linux interface (cpu_set_t, pthread_*affinity_np); elsewhere (e.g. macOS
// or Windows), the mask stays empty and pinning is not done
#ifdef __linux__
#include <sched.h>
#endif

namespace async::as {
struct CpuMask {
#ifdef __linux__
  cpu_set_t set;
#endif // __linux__

  void getaffinity_np([[maybe_unused]] pthread_t thread) {
#ifdef __linux__
    pthread_getaffinity_np(thread, sizeof(cpu_set_t), &set);
#endif
  }

  void setaffinity_np([[maybe_unused]] pthread_t thread) const {
#ifdef __linux__
    pthread_setaffinity_np(thread, sizeof(cpu_set_t), &set);
#endif
  }
};

} // namespace async::as
#endif // ASYNC_AS_PIN_H
