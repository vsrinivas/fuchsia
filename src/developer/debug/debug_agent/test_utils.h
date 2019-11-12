// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TEST_UTILS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TEST_UTILS_H_

#include <zircon/status.h>

namespace debug_agent {

// zx_status_t comparisong macro. Meand to be used with gtest, so that there is pretty printing for
// statuses on error.
//
// Left side: the statement to test.
// Right side, the expected zx_status_t.
//
// Example:
//
// ASSERT_ZX_EQ(SomethingReturningAStatus(), ZX_OK);
//
// Upon error, it will print something akin to:
//
// Expected: ZX_OK
// Got: ZX_ERR_BAD_STATE
#define ASSERT_ZX_EQ(stmt, expected)                                                          \
  {                                                                                           \
    zx_status_t status = (stmt);                                                              \
    ASSERT_EQ(status, expected) << "Expected " << zx_status_get_string(expected) << std::endl \
                                << "Got: " << zx_status_get_string(status);                   \
  }

#define EXPECT_ZX_EQ(stmt, expected)                                                          \
  {                                                                                           \
    zx_status_t status = (stmt);                                                              \
    EXPECT_EQ(status, expected) << "Expected " << zx_status_get_string(expected) << std::endl \
                                << "Got: " << zx_status_get_string(status);                   \
  }

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_TEST_UTILS_H_
