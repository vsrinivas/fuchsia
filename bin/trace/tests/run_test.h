// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_TESTS_RUN_TEST_H
#define GARNET_BIN_TRACE_TESTS_RUN_TEST_H

#include <string>

bool RunTspec(const std::string& tspec_file_path,
              const std::string& output_file_path);

bool VerifyTspec(const std::string& tspec_file_path,
                 const std::string& output_file_path);

#endif // GARNET_BIN_TRACE_TESTS_RUN_TEST_H
