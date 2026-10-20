// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#include <cxxtest/TestSuite.h>

#include "async/Config.h"

class TestConfig : public CxxTest::TestSuite {
  public:
  static void testDefaults() {
    const async::Config config;

    TS_ASSERT_EQUALS(config.mode(), async::Mode::Sync);
    TS_ASSERT_EQUALS(config.pinCore(), -1);
    TS_ASSERT_EQUALS(config.groupLayout().executorsPerNode, 1);
    TS_ASSERT_EQUALS(config.groupLayout().groupNodes, 1);
    TS_ASSERT_EQUALS(config.groupLayout().maxGroupRanks, 0);
    TS_ASSERT_EQUALS(config.useAsyncCopy(), true);
    TS_ASSERT_EQUALS(config.alignment(), 0);
  }

  static void testSetters() {
    async::Config config;

    config.setMode(async::Mode::Thread)
        .setPinCore(0)
        .setGroupLayout(async::GroupLayout{2, 4, 0})
        .setAlignment(2048);

    TS_ASSERT_EQUALS(config.mode(), async::Mode::Thread);
    TS_ASSERT_EQUALS(config.pinCore(), 0);
    TS_ASSERT_EQUALS(config.groupLayout().executorsPerNode, 2);
    TS_ASSERT_EQUALS(config.groupLayout().groupNodes, 4);
    TS_ASSERT_EQUALS(config.alignment(), 2048);
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
