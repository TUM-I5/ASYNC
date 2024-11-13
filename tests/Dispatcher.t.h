// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifdef USE_MPI
#include <mpi.h>
#endif // USE_MPI

#include <cxxtest/TestSuite.h>

#include "async/Dispatcher.h"

/**
 * Test the dispatcher for non MPI mode
 */
class TestDispatcher : public CxxTest::TestSuite {
  public:
  static void testGroupSize() {
    const async::Dispatcher dispatcher;
    TS_ASSERT_EQUALS(dispatcher.groupSize(), 1);
  }

  static void testGroupComm() {
    const async::Dispatcher dispatcher;
#ifdef USE_MPI
    TS_ASSERT_EQUALS(dispatcher.groupComm(), MPI_COMM_SELF);
#endif // USE_MPI
  }

  static void testCommWorld() {
    const async::Dispatcher dispatcher;
#ifdef USE_MPI
    TS_ASSERT_EQUALS(dispatcher.commWorld(), MPI_COMM_WORLD);
#endif // USE_MPI
  }
};