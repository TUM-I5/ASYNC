// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifndef ASYNC_CONFIG_H
#define ASYNC_CONFIG_H

#include <string>

#include "utils/env.h"
#include "utils/logger.h"
#include "utils/stringutils.h"

namespace async {

/**
 * The asynchnchronous mode that should be used
 */
enum Mode { SYNC, THREAD, MPI };

/**
 * @warning Overwriting the default values from the environment is only allowed
 *  before any other ASYNC class is created. Otherwise the behavior is undefined.
 */
class Config {
  private:
  Mode m_mode;

  int m_pinCore;

  unsigned int m_groupSize;

  bool m_asyncCopy;

  size_t m_alignment;

  Config()
      : m_mode(str2mode(utils::Env::get<const char*>("ASYNC_MODE", "SYNC"))),
        m_pinCore(utils::Env::get<int>("ASYNC_PIN_CORE", -1)),
        m_groupSize(m_mode == MPI ? utils::Env::get("ASYNC_GROUP_SIZE", 64) : 1),
        m_asyncCopy(utils::Env::get<int>("ASYNC_MPI_COPY", 0) != 0),
        m_alignment(utils::Env::get<size_t>("ASYNC_BUFFER_ALIGNMENT", 0)) {}

  public:
  static auto mode() -> Mode { return instance().m_mode; }

  static auto getPinCore() -> int { return instance().m_pinCore; }

  static auto groupSize() -> unsigned int { return instance().m_groupSize; }

  static auto useAsyncCopy() -> bool { return instance().m_asyncCopy; }

  static auto alignment() -> size_t { return instance().m_alignment; }

  static auto maxSend() -> size_t {
    return utils::Env::get<size_t>("ASYNC_MPI_MAX_SEND", 1UL << 30);
  }

  static void setMode(Mode mode) { instance().m_mode = mode; }

  static void setPinCore(int pinCore) { instance().m_pinCore = pinCore; }

  static void setUseAsyncCopy(bool useAsyncCopy) { instance().m_asyncCopy = useAsyncCopy; }

  static void setGroupSize(unsigned int groupSize) {
    if (Config::mode() == MPI) {
      instance().m_groupSize = groupSize;
    }
  }

  static void setAlignment(size_t alignment) { instance().m_alignment = alignment; }

  private:
  static auto instance() -> Config& {
    static Config config;
    return config;
  }

  static auto str2mode(const char* mode) -> Mode {
    std::string strMode(mode);
    utils::StringUtils::toUpper(strMode);

    if (strMode == "THREAD") {
      return THREAD;
    }
    if (strMode == "MPI") {
#ifdef USE_MPI
      return MPI;
#else  // USE_MPI
      logError() << "Asynchronous MPI is not supported without MPI";
#endif // USE_MPI
    }
    if (strMode != "SYNC") {
      logWarning() << "Unknown mode" << utils::nospace << strMode
                   << "for ASYNC output. Using synchronous mode.";
    }
    return SYNC;
  }
};

} // namespace async

#endif // ASYNC_CONFIG_H