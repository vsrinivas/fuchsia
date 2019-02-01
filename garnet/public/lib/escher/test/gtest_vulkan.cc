// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/escher/test/gtest_vulkan.h"

#include "garnet/public/lib/escher/test/gtest_vulkan_internal.h"

#include <vulkan/vulkan.hpp>

namespace testing {
namespace internal {
namespace escher {

// Wrapper around GTest's internal MakeAndRegisterTestInfo(), intended to
// support the VK_TEST() and VK_TEST_F() macros... see below.
GTEST_API_ TestInfo* MakeAndRegisterVulkanTestInfo(
    const char* test_case_name, const char* name, const char* type_param,
    const char* value_param, CodeLocation code_location,
    TypeId fixture_class_id, SetUpTestCaseFunc set_up_tc,
    TearDownTestCaseFunc tear_down_tc, TestFactoryFactory factory_factory) {
  if (::escher::VulkanIsSupported()) {
    return ::testing::internal::MakeAndRegisterTestInfo(
        test_case_name, name, type_param, value_param, code_location,
        fixture_class_id, set_up_tc, tear_down_tc, factory_factory());
  }
  return nullptr;
}

}  // namespace escher
}  // namespace internal
}  // namespace testing
