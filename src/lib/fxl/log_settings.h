// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FXL_LOG_SETTINGS_H_
#define SRC_LIB_FXL_LOG_SETTINGS_H_

#include <lib/syslog/cpp/log_settings.h>

namespace fxl {

using syslog::GetMinLogLevel;
using syslog::LogSettings;
using syslog::SetLogSettings;

inline void SetLogTags(const std::initializer_list<std::string>& tags) { syslog::SetTags(tags); }

}  // namespace fxl

#endif  // SRC_LIB_FXL_LOG_SETTINGS_H_
