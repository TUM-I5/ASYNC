// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <sched.h>

struct Parameter {
  Parameter() = default;

  int value{0};
};

template <class Test>
class Executor {
  private:
  Test& m_test;

  int m_schedCpu{};

  public:
  Executor(Test* test) : m_test(*test) {}

  void execInit(const Parameter& parameters) { m_test.setValue(parameters.value); }

  void exec(const Parameter& parameters) {
    m_test.setValue(parameters.value);

    //  Get the current core
    m_schedCpu = sched_getcpu();
  }

  /**
   * @return CPU id of the last execution
   */
  [[nodiscard]] auto cpu() const -> int { return m_schedCpu; }
};

#endif // EXECUTOR_H
