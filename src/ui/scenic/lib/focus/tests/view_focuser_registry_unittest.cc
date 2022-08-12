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
using SetAutoFocusResult = fuchsia::ui::views::Focuser_SetAutoFocus_Result;

constexpr zx_koid_t kFocuserKoid = 1;
constexpr zx_koid_t kFocuser2Koid = 2;
constexpr zx_koid_t kRandomKoid = 1124124214;

TEST(ViewFocuserRegistryTest, SuccessfulRequestFocus_ShouldReturnOK) {
  async::TestLoop test_loop;

  auto [control_ref, view_ref] = scenic::ViewRefPair::New();
  const zx_koid_t view_ref_koid = utils::ExtractKoid(view_ref);

  fuchsia::ui::views::FocuserPtr focuser;

  ViewFocuserRegistry registry(
      /*request_focus*/
      [=](zx_koid_t requestor, zx_koid_t request) {
        EXPECT_EQ(requestor, kFocuserKoid);
        EXPECT_EQ(request, view_ref_koid);
        return true;
      },
      /*set_auto_focus*/ [](auto...) { FAIL() << "Unexpected call to set_auto_focus"; });
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

  ViewFocuserRegistry registry(
      /*request_focus*/
      [=](zx_koid_t requestor, zx_koid_t request) {
        EXPECT_EQ(requestor, kFocuserKoid);
        EXPECT_EQ(request, view_ref_koid);
        return false;
      },
      /*set_auto_focus*/ [](auto...) { FAIL() << "Unexpected call to set_auto_focus"; });
  registry.Register(kFocuserKoid, focuser.NewRequest());
  test_loop.RunUntilIdle();

  std::optional<RequestFocusResult> result;
  focuser->RequestFocus(std::move(view_ref),
                        [&result](auto res) { result.emplace(std::move(res)); });
  test_loop.RunUntilIdle();

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->is_err());
}

TEST(ViewFocuserRegistryTest, SetAutoFocus_ShouldCallClosure) {
  async::TestLoop test_loop;

  auto [control_ref, view_ref] = scenic::ViewRefPair::New();
  const zx_koid_t view_ref_koid = utils::ExtractKoid(view_ref);

  fuchsia::ui::views::FocuserPtr focuser;
  zx_koid_t last_auto_focus_requestor = kRandomKoid;
  zx_koid_t last_auto_focus_target = kRandomKoid;
  ViewFocuserRegistry registry(
      /*request_focus*/ [](auto...) { return false; },
      /*set_auto_focus*/
      [&](zx_koid_t requestor, zx_koid_t target) {
        last_auto_focus_requestor = requestor;
        last_auto_focus_target = target;
      });
  registry.Register(kFocuserKoid, focuser.NewRequest());
  test_loop.RunUntilIdle();

  fuchsia::ui::views::FocuserSetAutoFocusRequest request{};
  request.set_view_ref(std::move(view_ref));

  bool callback_received = false;
  std::optional<SetAutoFocusResult> result;
  focuser->SetAutoFocus(std::move(request), [&callback_received, &result](auto res) {
    callback_received = true;
    result.emplace(std::move(res));
  });
  test_loop.RunUntilIdle();

  EXPECT_EQ(last_auto_focus_requestor, kFocuserKoid);
  EXPECT_EQ(last_auto_focus_target, view_ref_koid);
  EXPECT_TRUE(callback_received);

  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->is_err());
}

TEST(ViewFocuserRegistryTest, Empty_SetAutoFocus_ShouldCallClosureWithInvalidKoid) {
  async::TestLoop test_loop;

  fuchsia::ui::views::FocuserPtr focuser;
  zx_koid_t last_auto_focus_requestor = kRandomKoid;
  zx_koid_t last_auto_focus_target = kRandomKoid;
  ViewFocuserRegistry registry(
      /*request_focus*/ [](auto...) { return false; },
      /*set_auto_focus*/
      [&](zx_koid_t requestor, zx_koid_t target) {
        last_auto_focus_requestor = requestor;
        last_auto_focus_target = target;
      });
  registry.Register(kFocuserKoid, focuser.NewRequest());
  test_loop.RunUntilIdle();

  bool callback_received = false;
  std::optional<SetAutoFocusResult> result;
  focuser->SetAutoFocus(fuchsia::ui::views::FocuserSetAutoFocusRequest{},
                        [&callback_received, &result](auto res) {
                          callback_received = true;
                          result.emplace(std::move(res));
                        });
  test_loop.RunUntilIdle();

  EXPECT_EQ(last_auto_focus_requestor, kFocuserKoid);
  EXPECT_EQ(last_auto_focus_target, ZX_KOID_INVALID);
  EXPECT_TRUE(callback_received);

  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->is_err());
}

TEST(ViewFocuserRegistryTest, OnChannelClosure_EndpointShouldBeCleanedUp) {
  async::TestLoop test_loop;

  // Register two focusers.
  zx_koid_t last_auto_focus_requestor = kRandomKoid;
  zx_koid_t last_auto_focus_target = kRandomKoid;
  ViewFocuserRegistry registry(
      /*request_focus*/ [](auto...) { return true; },
      /*set_auto_focus*/
      [&](zx_koid_t requestor, zx_koid_t target) {
        last_auto_focus_requestor = requestor;
        last_auto_focus_target = target;
      });
  EXPECT_TRUE(registry.endpoints().empty());

  fuchsia::ui::views::FocuserPtr focuser1;
  registry.Register(kFocuserKoid, focuser1.NewRequest());
  test_loop.RunUntilIdle();
  EXPECT_EQ(registry.endpoints().size(), 1u);
  EXPECT_TRUE(registry.endpoints().count(kFocuserKoid) == 1);

  EXPECT_EQ(last_auto_focus_requestor, kRandomKoid);
  EXPECT_EQ(last_auto_focus_target, kRandomKoid);

  fuchsia::ui::views::FocuserPtr focuser2;
  registry.Register(kFocuser2Koid, focuser2.NewRequest());
  test_loop.RunUntilIdle();
  EXPECT_EQ(registry.endpoints().size(), 2u);
  EXPECT_TRUE(registry.endpoints().count(kFocuser2Koid) == 1);

  EXPECT_EQ(last_auto_focus_requestor, kRandomKoid);
  EXPECT_EQ(last_auto_focus_target, kRandomKoid);

  // Close one and watch it clean up.
  focuser1.Unbind();
  test_loop.RunUntilIdle();
  EXPECT_EQ(registry.endpoints().size(), 1u);
  EXPECT_TRUE(registry.endpoints().count(kFocuserKoid) == 0);
  EXPECT_EQ(last_auto_focus_requestor, kFocuserKoid);
  EXPECT_EQ(last_auto_focus_target, ZX_KOID_INVALID);

  // Close the other one.
  focuser2.Unbind();
  test_loop.RunUntilIdle();
  EXPECT_TRUE(registry.endpoints().empty());
  EXPECT_EQ(last_auto_focus_requestor, kFocuser2Koid);
  EXPECT_EQ(last_auto_focus_target, ZX_KOID_INVALID);
}

}  // namespace focus::test
