// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#include <cxxtest/TestSuite.h>

#include "async/Config.h"

/**
 * @todo Reset the config after each test
 */
class TestConfig : public CxxTest::TestSuite {
  public:
  static void testMode() {
    TS_ASSERT_EQUALS(async::Config::mode(), async::Mode::Sync);

    async::Config::setMode(async::Mode::Thread);
    TS_ASSERT_EQUALS(async::Config::mode(), async::Mode::Thread);
  }

  static void testGetPinCore() {
    TS_ASSERT_EQUALS(async::Config::getPinCore(), -1);

    async::Config::setPinCore(0);
    TS_ASSERT_EQUALS(async::Config::getPinCore(), 0);
  }

  static void testGroupSize() {
    TS_ASSERT_EQUALS(async::Config::groupSize(), 1);

    async::Config::setGroupSize(4);
    TS_ASSERT_EQUALS(async::Config::groupSize(), 1);

    async::Config::setMode(async::Mode::MPI);
    async::Config::setGroupSize(4);
    TS_ASSERT_EQUALS(async::Config::groupSize(), 4);
  }

  static void testUseAsyncCopy() {
    TS_ASSERT_EQUALS(async::Config::useAsyncCopy(), false);

    async::Config::setUseAsyncCopy(true);
    TS_ASSERT_EQUALS(async::Config::useAsyncCopy(), true);
  }

  static void testAlignment() {
    TS_ASSERT_EQUALS(async::Config::alignment(), 0);

    async::Config::setAlignment(2048);
    TS_ASSERT_EQUALS(async::Config::alignment(), 2048);
  }
};
