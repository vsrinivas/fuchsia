// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_INFERIOR_CONTROL_TEST_HELPER_H_
#define GARNET_LIB_INFERIOR_CONTROL_TEST_HELPER_H_

#include <stdint.h>

namespace inferior_control {

// We need a place to record this for use by various test files.
constexpr const char kTestHelperPath[] = "/pkg/bin/test_helper";

// A string that appears in the Dso name of the test helper executable.
constexpr const char kTestHelperDsoName[] = "test_helper";

}  // namespace inferior_control

#endif // GARNET_LIB_INFERIOR_CONTROL_TEST_HELPER_H_
