// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_DEBUGGER_UTILS_TEST_HELPER_H_
#define GARNET_LIB_DEBUGGER_UTILS_TEST_HELPER_H_

namespace debugger_utils {

// We need a place to record this for use by various test files.
constexpr const char kTestHelperPath[] = "/pkg/bin/test_helper";

// A special value to pass between processes as a sanity check.
constexpr uint64_t kUint64MagicPacketValue = 0x0123456789abcdeful;

}  // namespace debugger_utils

#endif // GARNET_LIB_DEBUGGER_UTILS_TEST_HELPER_H_
