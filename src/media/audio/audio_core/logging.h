// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_LOGGING_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_LOGGING_H_

#include <iomanip>

#include "src/lib/fxl/log_settings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"

#define AUD_LOG(level) \
  FXL_LOG(level) << zx::clock::get_monotonic().get() << " " << std::setw(25) << __func__
#define AUD_LOG_OBJ(level, object) AUD_LOG(level) << " for " << reinterpret_cast<void*>(object)

#define AUD_VLOG(level) \
  FXL_VLOG(level) << zx::clock::get_monotonic().get() << " " << std::setw(25) << __func__
#define AUD_VLOG_OBJ(level, object) AUD_VLOG(level) << " for " << reinterpret_cast<void*>(object)

namespace media::audio {

// Custom FXL_VLOG levels
constexpr auto TRACE = 1;
constexpr auto SPEW = 2;

class Logging {
 public:
  Logging() = delete;

  static void Init() {
    fxl::LogSettings settings;

    // For verbose logging, set to -media::audio::TRACE or -media::audio::SPEW
#ifdef NDEBUG
    settings.min_log_level = fxl::LOG_WARNING;
#else
    settings.min_log_level = fxl::LOG_INFO;
#endif

    fxl::SetLogSettings(settings);
  }
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_LOGGING_H_
