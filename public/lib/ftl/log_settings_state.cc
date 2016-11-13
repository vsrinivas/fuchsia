// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/log_settings_state.h"

#include <algorithm>

namespace ftl {
namespace {

LogSettings g_log_settings;

}  // namespace

void SetLogSettings(const LogSettings& settings) {
  // Validate the new settings as we set them.
  g_log_settings.min_log_level = std::min(LOG_FATAL, settings.min_log_level);
}

LogSettings GetLogSettings() {
  return g_log_settings;
}

int GetMinLogLevel() {
  return g_log_settings.min_log_level;
}

}  // namespace ftl
