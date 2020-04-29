// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FXL_LOGGING_BACKEND_H_
#define SRC_LIB_FXL_LOGGING_BACKEND_H_

#include "src/lib/fxl/log_settings.h"

namespace fxl_logging_backend {

void SetSettings(const fxl::LogSettings& settings);

void SetSettings(const fxl::LogSettings& settings, const std::initializer_list<std::string>& tags);

fxl::LogSeverity GetMinLogLevel();

}  // namespace fxl_logging_backend

#endif  // SRC_LIB_FXL_LOGGING_BACKEND_H_
