// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#include <mpi.h>

#include <cxxtest/TestSuite.h>

#include "async/Config.h"
#include "async/as/MPIScheduler.h"

#include <algorithm>
#include <vector>

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
      break;
    default:
      TS_ASSERT(false);
      break;
    }
  }
  /**
   * Node boundaries drawn by the test, so that a single machine can stand in
   * for several nodes: the scheduler takes them as a communicator per node
   */
  static auto fakeNodes(int color) -> MPI_Comm {
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm nodeComm = MPI_COMM_NULL;
    MPI_Comm_split(MPI_COMM_WORLD, color, rank, &nodeComm);
    return nodeComm;
  }

  /**
   * @return Whether all members of the group of this rank have the same color
   */
  static auto groupHasColor(const async::as::MPIScheduler& scheduler, int color) -> bool {
    int size = 0;
    MPI_Comm_size(scheduler.groupComm(), &size);
    std::vector<int> colors(size);
    MPI_Allgather(&color, 1, MPI_INT, colors.data(), 1, MPI_INT, scheduler.groupComm());
    return std::all_of(colors.begin(), colors.end(), [color](int other) { return other == color; });
  }

  /** Nodes {0, 1, 2} and {3, 4}, one executor each */
  void testNodeBlock() const {
    const int node = m_rank / 3;
    MPI_Comm nodeComm = fakeNodes(node);

    async::as::MPIScheduler scheduler;
    scheduler.setCommunicator(MPI_COMM_WORLD, nodeComm, async::GroupLayout{});
    MPI_Comm_free(&nodeComm);

    TS_ASSERT_EQUALS(scheduler.isExecutor(), m_rank == 2 || m_rank == 4);
    TS_ASSERT(scheduler.isCompute());
    TS_ASSERT(groupHasColor(scheduler, node));
    TS_ASSERT_EQUALS(scheduler.numComputeRanks(), node == 0 ? 3 : 2);

    MPI_Comm execComm = MPI_COMM_NULL;
    MPI_Comm_split(MPI_COMM_WORLD, scheduler.isExecutor() ? 0 : MPI_UNDEFINED, 0, &execComm);
    scheduler.determineOrder(execComm);
    if (execComm != MPI_COMM_NULL) {
      MPI_Comm_free(&execComm);
    }

    const auto& layout = scheduler.layout();
    TS_ASSERT(layout.rankOrdered);
    TS_ASSERT_EQUALS(layout.nodes, 2);
    TS_ASSERT_EQUALS(layout.executors, 2);
    TS_ASSERT_EQUALS(layout.executorsPerNode, 1);
    TS_ASSERT_EQUALS(layout.minGroupRanks, 2);
    TS_ASSERT_EQUALS(layout.maxGroupRanks, 3);
  }

  /**
   * Nodes {0, 2, 4} and {1, 3}, as a round-robin placement leaves them: the
   * groups still follow the nodes rather than the rank numbers
   */
  void testNodeCyclic() const {
    const int node = m_rank % 2;
    MPI_Comm nodeComm = fakeNodes(node);

    async::as::MPIScheduler scheduler;
    scheduler.setCommunicator(MPI_COMM_WORLD, nodeComm, async::GroupLayout{});
    MPI_Comm_free(&nodeComm);

    TS_ASSERT(groupHasColor(scheduler, node));
    // the last rank of each node
    TS_ASSERT_EQUALS(scheduler.isExecutor(), m_rank == 4 || m_rank == 3);
    TS_ASSERT_EQUALS(scheduler.numComputeRanks(), node == 0 ? 3 : 2);
    TS_ASSERT_EQUALS(scheduler.layout().nodes, 2);

    // the executors hold ranks 0, 2, 4 and then 1, 3
    MPI_Comm execComm = MPI_COMM_NULL;
    MPI_Comm_split(MPI_COMM_WORLD, scheduler.isExecutor() ? 0 : MPI_UNDEFINED, 0, &execComm);
    scheduler.determineOrder(execComm);
    if (execComm != MPI_COMM_NULL) {
      MPI_Comm_free(&execComm);
    }
    TS_ASSERT(!scheduler.layout().rankOrdered);
  }

  /** Two executors on each node of {0, 1, 2} and {3, 4} */
  void testExecutorsPerNode() const {
    const int node = m_rank / 3;
    MPI_Comm nodeComm = fakeNodes(node);

    async::GroupLayout groupLayout;
    groupLayout.executorsPerNode = 2;

    async::as::MPIScheduler scheduler;
    scheduler.setCommunicator(MPI_COMM_WORLD, nodeComm, groupLayout);
    MPI_Comm_free(&nodeComm);

    // groups {0, 1}, {2}, {3} and {4}
    TS_ASSERT(groupHasColor(scheduler, node));
    TS_ASSERT_EQUALS(scheduler.isExecutor(), m_rank != 0);
    TS_ASSERT_EQUALS(scheduler.layout().executors, 4);
    TS_ASSERT_EQUALS(scheduler.layout().minGroupRanks, 1);
    TS_ASSERT_EQUALS(scheduler.layout().maxGroupRanks, 2);
  }

  /**
   * A cap on the ranks per executor raises the executors of every node to
   * what the fullest node needs
   */
  void testMaxGroupRanks() const {
    const int node = m_rank / 3;
    MPI_Comm nodeComm = fakeNodes(node);

    async::GroupLayout groupLayout;
    groupLayout.maxGroupRanks = 2;

    async::as::MPIScheduler scheduler;
    scheduler.setCommunicator(MPI_COMM_WORLD, nodeComm, groupLayout);
    MPI_Comm_free(&nodeComm);

    TS_ASSERT(groupHasColor(scheduler, node));
    TS_ASSERT_EQUALS(scheduler.layout().executorsPerNode, 2);
    TS_ASSERT_EQUALS(scheduler.layout().executors, 4);
    TS_ASSERT_LESS_THAN_EQUALS(scheduler.numComputeRanks(), 2);
  }

  /** One set of executors for two nodes */
  void testGroupNodes() const {
    MPI_Comm nodeComm = fakeNodes(m_rank / 3);

    async::GroupLayout groupLayout;
    groupLayout.groupNodes = 2;

    async::as::MPIScheduler scheduler;
    scheduler.setCommunicator(MPI_COMM_WORLD, nodeComm, groupLayout);
    MPI_Comm_free(&nodeComm);

    TS_ASSERT_EQUALS(scheduler.isExecutor(), m_rank == 4);
    TS_ASSERT_EQUALS(scheduler.numComputeRanks(), 5);
    TS_ASSERT_EQUALS(scheduler.layout().nodes, 2);
    TS_ASSERT_EQUALS(scheduler.layout().executors, 1);
  }

  /** Shared memory can be turned off, which leaves the messages */
  void testNoSharedMemory() const {
    async::as::MPIScheduler scheduler;
    scheduler.setAllowSharedMemory(false);
    scheduler.setCommunicator(MPI_COMM_WORLD, async::GroupLayout{});

    TS_ASSERT(!scheduler.sharedMemory());
  }

  /** A group whose ranks all share memory aggregates in shared windows by default */
  void testSharedMemory() const {
    async::as::MPIScheduler scheduler;
    scheduler.setCommunicator(MPI_COMM_WORLD, async::GroupLayout{});

    TS_ASSERT(scheduler.sharedMemory());
  }

  /** Executors that only collect: the application loses one rank per node */
  void testExclusiveNodes() const {
    const int node = m_rank / 3;
    MPI_Comm nodeComm = fakeNodes(node);

    async::GroupLayout groupLayout;
    groupLayout.exclusive = true;

    async::as::MPIScheduler scheduler;
    scheduler.setCommunicator(MPI_COMM_WORLD, nodeComm, groupLayout);
    MPI_Comm_free(&nodeComm);

    const bool executor = m_rank == 2 || m_rank == 4;
    TS_ASSERT_EQUALS(scheduler.isExecutor(), executor);
    TS_ASSERT_EQUALS(scheduler.isCompute(), !executor);
    TS_ASSERT_EQUALS(scheduler.numComputeRanks(), node == 0 ? 2 : 1);

    int computeRanks = 0;
    MPI_Comm_size(scheduler.commWorld(), &computeRanks);
    TS_ASSERT_EQUALS(computeRanks, executor ? 2 : 3);

    TS_ASSERT(scheduler.layout().exclusive);
    TS_ASSERT_EQUALS(scheduler.layout().minGroupRanks, 1);
    TS_ASSERT_EQUALS(scheduler.layout().maxGroupRanks, 2);
  }
};
