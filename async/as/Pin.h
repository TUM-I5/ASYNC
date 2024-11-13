// SPDX-FileCopyrightText: 2023-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ASYNC_AS_PIN_H
#define ASYNC_AS_PIN_H

#include <pthread.h>

#ifndef __APPLE__
#include <sched.h>
#endif

namespace async::as {
struct CpuMask {
#ifndef __APPLE__
  cpu_set_t set;
#endif // __APPLE__

  void getaffinity_np([[maybe_unused]] pthread_t thread) {
#ifndef __APPLE__
    pthread_getaffinity_np(thread, sizeof(cpu_set_t), &set);
#endif
  }

  void setaffinity_np([[maybe_unused]] pthread_t thread) const {
#ifndef __APPLE__
    pthread_setaffinity_np(thread, sizeof(cpu_set_t), &set);
#endif
  }
};

} // namespace async::as
#endif // ASYNC_AS_PIN_H
