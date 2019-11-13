// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_LOGGING_LOGGING_H_
#define SRC_MEDIA_AUDIO_LIB_LOGGING_LOGGING_H_

#include <lib/zx/clock.h>

#include <iomanip>

#include "src/lib/syslog/cpp/logger.h"

#define AUD_LOG(level)                                                                          \
  FX_LOGS(level) << std::fixed << std::setprecision(3)                                          \
                 << (static_cast<double>(zx::clock::get_monotonic().get()) / zx::msec(1).get()) \
                 << " " << std::setw(25) << __func__ << " "
#define AUD_LOG_OBJ(level, object) \
  AUD_LOG(level) << "for " << reinterpret_cast<void*>(object) << " "

#define AUD_VLOG(level)                                                                          \
  FX_VLOGS(level) << std::fixed << std::setprecision(3)                                          \
                  << (static_cast<double>(zx::clock::get_monotonic().get()) / zx::msec(1).get()) \
                  << " " << std::setw(25) << __func__ << " "
#define AUD_VLOG_OBJ(level, object) \
  AUD_VLOG(level) << "for " << reinterpret_cast<const void*>(object) << " "

namespace media::audio {

// Custom FX_VLOGS levels
constexpr auto TRACE = 1;
constexpr auto SPEW = 2;

class Logging {
 public:
  Logging() = delete;

  static void Init(const fx_log_severity_t log_level,
                   const std::initializer_list<std::string>& tags) {
    syslog::LogSettings settings = { .severity = log_level, .fd = -1 };
    syslog::InitLogger(settings, tags);
  }
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_LOGGING_LOGGING_H_
