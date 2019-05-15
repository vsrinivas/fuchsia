// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_LIB_FXL_TEST_TEST_SETTINGS_H_
#define SRC_LIB_FXL_TEST_TEST_SETTINGS_H_

#include "src/lib/fxl/command_line.h"

namespace fxl {
// Sets test-related settings from `command_line` parameters:
// - logging (see src/lib/fxl/log_settings_command_line.h)
// - --test_loop_seed for TestLoop's random seed
// Returns true if parsing succeeded.
bool SetTestSettings(const CommandLine& command_line);

bool SetTestSettings(int argc, const char* const* argv);

}  // namespace fxl

#endif  // SRC_LIB_FXL_TEST_TEST_SETTINGS_H_
