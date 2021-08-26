// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_TESTS_RUN_TEST_H_
#define GARNET_BIN_CPUPERF_TESTS_RUN_TEST_H_

#include <lib/syslog/cpp/log_settings.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>

#include <string>
#include <vector>

bool RunSpec(const std::string& spec_file_path, const syslog::LogSettings& log_settings);

#endif  // GARNET_BIN_CPUPERF_TESTS_RUN_TEST_H_
