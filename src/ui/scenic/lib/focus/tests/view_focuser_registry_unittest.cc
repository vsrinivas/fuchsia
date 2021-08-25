// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/focus/view_focuser_registry.h"

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-testing/test_loop.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/zx/time.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/utils/helpers.h"

namespace focus::test {

using RequestFocusResult = fuchsia::ui::views::Focuser_RequestFocus_Result;

constexpr zx_koid_t kFocuserKoid = 1;
constexpr zx_koid_t kFocuser2Koid = 2;

TEST(ViewFocuserRegistryTest, SuccessfulRequestFocus_ShouldReturnOK) {
  async::TestLoop test_loop;

  auto [control_ref, view_ref] = scenic::ViewRefPair::New();
  const zx_koid_t view_ref_koid = utils::ExtractKoid(view_ref);

  fuchsia::ui::views::FocuserPtr focuser;

  ViewFocuserRegistry registry([=](zx_koid_t requestor, zx_koid_t request) {
    EXPECT_EQ(requestor, kFocuserKoid);
    EXPECT_EQ(request, view_ref_koid);
    return true;
  });
  registry.Register(kFocuserKoid, focuser.NewRequest());
  test_loop.RunUntilIdle();

  std::optional<RequestFocusResult> result;
  focuser->RequestFocus(std::move(view_ref),
                        [&result](auto res) { result.emplace(std::move(res)); });
  test_loop.RunUntilIdle();

  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->is_err());
}

TEST(ViewFocuserRegistryTest, FailedRequestFocus_ShouldReturnError) {
  async::TestLoop test_loop;

  auto [control_ref, view_ref] = scenic::ViewRefPair::New();
  const zx_koid_t view_ref_koid = utils::ExtractKoid(view_ref);

  fuchsia::ui::views::FocuserPtr focuser;

  ViewFocuserRegistry registry([=](zx_koid_t requestor, zx_koid_t request) {
    EXPECT_EQ(requestor, kFocuserKoid);
    EXPECT_EQ(request, view_ref_koid);
    return false;  // unconditionally deny
  });
  registry.Register(kFocuserKoid, focuser.NewRequest());
  test_loop.RunUntilIdle();

  std::optional<RequestFocusResult> result;
  focuser->RequestFocus(std::move(view_ref),
                        [&result](auto res) { result.emplace(std::move(res)); });
  test_loop.RunUntilIdle();

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->is_err());
}

TEST(ViewFocuserRegistryTest, OnChannelClosure_EndpointShouldBeCleanedUp) {
  async::TestLoop test_loop;

  // Register two focusers.
  ViewFocuserRegistry registry([](auto...) { return true; });
  EXPECT_TRUE(registry.endpoints().empty());

  fuchsia::ui::views::FocuserPtr focuser1;
  registry.Register(kFocuserKoid, focuser1.NewRequest());
  test_loop.RunUntilIdle();
  EXPECT_EQ(registry.endpoints().size(), 1u);
  EXPECT_TRUE(registry.endpoints().count(kFocuserKoid) == 1);

  fuchsia::ui::views::FocuserPtr focuser2;
  registry.Register(kFocuser2Koid, focuser2.NewRequest());
  test_loop.RunUntilIdle();
  EXPECT_EQ(registry.endpoints().size(), 2u);
  EXPECT_TRUE(registry.endpoints().count(kFocuser2Koid) == 1);

  // Close one and watch it clean up.
  focuser1.Unbind();
  test_loop.RunUntilIdle();
  EXPECT_EQ(registry.endpoints().size(), 1u);
  EXPECT_TRUE(registry.endpoints().count(kFocuserKoid) == 0);

  // Close the other one.
  focuser2.Unbind();
  test_loop.RunUntilIdle();
  EXPECT_TRUE(registry.endpoints().empty());
}

}  // namespace focus::test
