// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_LOGGING_LOGGING_H_
#define SRC_MEDIA_AUDIO_LIB_LOGGING_LOGGING_H_

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include <iomanip>

#define AUDIO_LOG(level)                                                                        \
  FX_LOGS(level) << std::fixed << std::setprecision(3)                                          \
                 << (static_cast<double>(zx::clock::get_monotonic().get()) / zx::msec(1).get()) \
                 << " " << std::setw(25) << __func__ << " "
#define AUDIO_LOG_OBJ(level, object) \
  AUDIO_LOG(level) << "for " << reinterpret_cast<void*>(object) << " "

#endif  // SRC_MEDIA_AUDIO_LIB_LOGGING_LOGGING_H_
