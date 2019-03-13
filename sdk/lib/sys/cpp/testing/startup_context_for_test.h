// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_CPP_TESTING_STARTUP_CONTEXT_FOR_TEST_H_
#define LIB_SYS_CPP_TESTING_STARTUP_CONTEXT_FOR_TEST_H_

#include <lib/sys/cpp/startup_context.h>
#include <lib/sys/cpp/testing/component_context_for_test.h>

namespace sys {
namespace testing {

// TODO: Remove once all clients migrate.
using StartupContextForTest = ComponentContextForTest;

}  // namespace testing
}  // namespace sys

#endif  // LIB_SYS_CPP_TESTING_STARTUP_CONTEXT_FOR_TEST_H_
