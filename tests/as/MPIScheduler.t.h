// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#include <mpi.h>

#include <cxxtest/TestSuite.h>

#include "async/as/MPIScheduler.h"

class TestMPIScheduler : public CxxTest::TestSuite {
  int m_rank{};

  public:
  void setUp() override { MPI_Comm_rank(MPI_COMM_WORLD, &m_rank); }

  void testIsExecutor() const {
    async::as::MPIScheduler scheduler;
    scheduler.setCommunicator(MPI_COMM_WORLD, 2);

    switch (m_rank) {
    case 2:
    case 4:
      TS_ASSERT(scheduler.isExecutor());
      break;
    default:
      TS_ASSERT(!scheduler.isExecutor());
    }
  }

  void testCommWorld() const {
    async::as::MPIScheduler scheduler;
    scheduler.setCommunicator(MPI_COMM_WORLD, 2);

    int size = 0;
    MPI_Comm_size(scheduler.commWorld(), &size);

    switch (m_rank) {
    case 2:
    case 4:
      TS_ASSERT_EQUALS(size, 2);
      break;
    default:
      TS_ASSERT_EQUALS(size, 3);
    }
  }

  void testGroupComm() const {
    async::as::MPIScheduler scheduler;
    scheduler.setCommunicator(MPI_COMM_WORLD, 2);

    TS_ASSERT_EQUALS(scheduler.groupRank(), m_rank % 3);

    int size = 0;

    switch (m_rank) {
    case 2:
    case 4:
      TS_ASSERT_EQUALS(scheduler.groupComm(), MPI_COMM_NULL);
      break;
    case 0:
    case 1:
      MPI_Comm_size(scheduler.groupComm(), &size);
      TS_ASSERT_EQUALS(size, 2);
      break;
    case 3:
      MPI_Comm_size(scheduler.groupComm(), &size);
      TS_ASSERT_EQUALS(size, 1);
    }
  }
};
