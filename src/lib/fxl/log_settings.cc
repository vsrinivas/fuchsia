// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fxl/log_settings.h"

#include "src/lib/fxl/logging_backend.h"

namespace fxl {

void SetLogSettings(const LogSettings& settings) { fxl_logging_backend::SetSettings(settings); }

void SetLogSettings(const LogSettings& settings, const std::initializer_list<std::string>& tags) {
  fxl_logging_backend::SetSettings(settings, tags);
}

void SetLogTags(const std::initializer_list<std::string>& tags) {
  SetLogSettings({.min_log_level = GetMinLogLevel()}, tags);
}

int GetMinLogLevel() { return fxl_logging_backend::GetMinLogLevel(); }

}  // namespace fxl
