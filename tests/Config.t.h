// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#include <cxxtest/TestSuite.h>

#include <stdlib.h>

#include "async/Config.h"
#include "utils/env.h"

class TestConfig : public CxxTest::TestSuite {
  public:
  static void testDefaults() {
    const async::Config config;

    TS_ASSERT_EQUALS(config.mode(), async::Mode::Sync);
    TS_ASSERT_EQUALS(config.pinCore(), -1);
    TS_ASSERT_EQUALS(config.groupLayout().executorsPerNode, 1);
    TS_ASSERT_EQUALS(config.groupLayout().groupNodes, 1);
    TS_ASSERT_EQUALS(config.groupLayout().maxGroupRanks, 0);
    TS_ASSERT_EQUALS(config.groupLayout().exclusive, false);
    TS_ASSERT_EQUALS(config.useAsyncCopy(), true);
    TS_ASSERT_EQUALS(config.alignment(), 0);
    TS_ASSERT_EQUALS(config.maxSend(), 1UL << 30);
    TS_ASSERT_EQUALS(config.pollIntervalUs(), -1);
    TS_ASSERT_EQUALS(config.sharedMemory(), true);
  }

  static void testSetters() {
    async::Config config;

    config.setMode(async::Mode::Thread)
        .setPinCore(0)
        .setGroupLayout(async::GroupLayout{2, 4, 0})
        .setAlignment(2048)
        .setUseAsyncCopy(false)
        .setMaxSend(4096)
        .setPollIntervalUs(250)
        .setSharedMemory(false);

    TS_ASSERT_EQUALS(config.mode(), async::Mode::Thread);
    TS_ASSERT_EQUALS(config.pinCore(), 0);
    TS_ASSERT_EQUALS(config.groupLayout().executorsPerNode, 2);
    TS_ASSERT_EQUALS(config.groupLayout().groupNodes, 4);
    TS_ASSERT_EQUALS(config.alignment(), 2048);
    TS_ASSERT_EQUALS(config.useAsyncCopy(), false);
    TS_ASSERT_EQUALS(config.maxSend(), 4096);
    TS_ASSERT_EQUALS(config.pollIntervalUs(), 250);
    TS_ASSERT_EQUALS(config.sharedMemory(), false);
  }

  /**
   * An application reads the settings under a prefix of its own first, and
   * under the plain one after that
   */
  static void testPrefixes() {
    setenv("TESTAPP_ASYNC_MODE", "THREAD", 1);
    setenv("TESTPLAIN_ASYNC_MODE", "SYNC", 1);
    setenv("TESTPLAIN_ASYNC_EXECUTORS_PER_NODE", "3", 1);

    const async::Config config{utils::Env{"TESTAPP_ASYNC_", "TESTPLAIN_ASYNC_"}};

    unsetenv("TESTAPP_ASYNC_MODE");
    unsetenv("TESTPLAIN_ASYNC_MODE");
    unsetenv("TESTPLAIN_ASYNC_EXECUTORS_PER_NODE");

    TS_ASSERT_EQUALS(config.mode(), async::Mode::Thread);
    TS_ASSERT_EQUALS(config.groupLayout().executorsPerNode, 3);
  }

  /** The deprecated GROUP_SIZE bounds the ranks per executor */
  static void testGroupSize() {
    setenv("TESTAPP_ASYNC_GROUP_SIZE", "5", 1);

    const async::Config config{utils::Env{"TESTAPP_ASYNC_"}};

    unsetenv("TESTAPP_ASYNC_GROUP_SIZE");

    TS_ASSERT_EQUALS(config.groupLayout().maxGroupRanks, 5);
  }

  static void testIndependence() {
    async::Config first;
    const async::Config second;

    first.setAlignment(4096);

    TS_ASSERT_EQUALS(first.alignment(), 4096);
    TS_ASSERT_EQUALS(second.alignment(), 0);
  }

  static void testModeParsing() {
    TS_ASSERT_EQUALS(async::Config::str2mode("thread"), async::Mode::Thread);
    TS_ASSERT_EQUALS(async::Config::str2mode("SYNC"), async::Mode::Sync);
    TS_ASSERT_EQUALS(async::Config::str2mode("nonsense"), async::Mode::Sync);
#ifdef USE_MPI
    TS_ASSERT_EQUALS(async::Config::str2mode("node"), async::Mode::MPI);
    // the deprecated name
    TS_ASSERT_EQUALS(async::Config::str2mode("MPI"), async::Mode::MPI);
#endif // USE_MPI
  }
};
