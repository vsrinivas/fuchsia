// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_TEST_GTEST_ESCHER_H_
#define LIB_ESCHER_TEST_GTEST_ESCHER_H_

#include "garnet/public/lib/escher/escher.h"
#include "garnet/public/lib/escher/test/gtest_vulkan.h"

namespace escher {
namespace test {

// Must call during tests, only if !VK_TESTS_SUPPRESSED().
// SetUpTestEscher() must have already been called, and TearDownTestEscher()
// must not have already been called.
Escher* GetEscher();

// Call before running tests, typically in main().
void SetUpEscher();

// Call after running tests, typically in main().
void TearDownEscher();

}  // namespace test
}  // namespace escher

#endif  // LIB_ESCHER_TEST_GTEST_ESCHER_H_
