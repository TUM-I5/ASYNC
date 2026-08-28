// SPDX-FileCopyrightText: 2016 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileContributor: Sebastian Rettenberger <sebastian.rettenberger@tum.de>

#ifndef ASYNC_CONFIG_H
#define ASYNC_CONFIG_H

#include <cstddef>
#include <string>

#include "utils/env.h"
#include "utils/logger.h"
#include "utils/stringutils.h"

namespace async {

/**
 * The asynchnchronous mode that should be used
 */
enum class Mode { Sync, Thread, MPI };

/**
 * The settings of a single {@link Dispatcher} and of the {@link Module}s
 * attached to it.
 *
 * A Config is a plain value. It reads its defaults from the environment once,
 * at construction; the setters override them afterwards. The environment
 * prefix is supplied by the caller, so an application can namespace the
 * variables:
 *
 * @code
 * async::Config config{utils::Env{"MYAPP_ASYNC_", "ASYNC_"}};
 * @endcode
 */
class Config {
  private:
  Mode m_mode;
  int m_pinCore;
  unsigned int m_groupSize;
  bool m_asyncCopy;
  size_t m_alignment;
  size_t m_maxSend;

  public:
  Config() : Config(utils::Env{"ASYNC_"}) {}

  explicit Config(utils::Env env)
      : m_mode(str2mode(env.getOptional<std::string>("MODE").value_or("SYNC"))),
        m_pinCore(env.get<int>("PIN_CORE", -1)),
        m_groupSize(m_mode == Mode::MPI ? env.get("GROUP_SIZE", 64) : 1),
        m_asyncCopy(env.get<bool>("MPI_COPY", true)),
        m_alignment(env.get<size_t>("BUFFER_ALIGNMENT", 0)),
        m_maxSend(env.get<size_t>("MPI_MAX_SEND", 1UL << 30)) {}

  [[nodiscard]] auto mode() const -> Mode { return m_mode; }

  [[nodiscard]] auto pinCore() const -> int { return m_pinCore; }

  [[nodiscard]] auto groupSize() const -> unsigned int { return m_groupSize; }

  [[nodiscard]] auto useAsyncCopy() const -> bool { return m_asyncCopy; }

  [[nodiscard]] auto alignment() const -> size_t { return m_alignment; }

  [[nodiscard]] auto maxSend() const -> size_t { return m_maxSend; }

  auto setMode(Mode mode) -> Config& {
    m_mode = mode;
    return *this;
  }

  auto setPinCore(int pinCore) -> Config& {
    m_pinCore = pinCore;
    return *this;
  }

  auto setGroupSize(unsigned int groupSize) -> Config& {
    m_groupSize = groupSize;
    return *this;
  }

  auto setUseAsyncCopy(bool useAsyncCopy) -> Config& {
    m_asyncCopy = useAsyncCopy;
    return *this;
  }

  auto setAlignment(size_t alignment) -> Config& {
    m_alignment = alignment;
    return *this;
  }

  auto setMaxSend(size_t maxSend) -> Config& {
    m_maxSend = maxSend;
    return *this;
  }

  static auto str2mode(const std::string& mode) -> Mode {
    std::string strMode(mode);
    utils::StringUtils::toUpper(strMode);

    if (strMode == "THREAD") {
      return Mode::Thread;
    }
    if (strMode == "MPI") {
#ifdef USE_MPI
      return Mode::MPI;
#else  // USE_MPI
      logError() << "Asynchronous MPI is not supported without MPI";
#endif // USE_MPI
    }
    if (strMode != "SYNC") {
      logWarning() << "Unknown mode" << utils::nospace << strMode
                   << "for ASYNC output. Using synchronous mode.";
    }
    return Mode::Sync;
  }
};

} // namespace async

#endif // ASYNC_CONFIG_H
