// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/view_ref_installed_impl.h"

#include <lib/async-testing/test_loop.h>
#include <lib/async/default.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/scenic/lib/utils/helpers.h"

namespace lib_ui_gfx_engine_tests {

using fuchsia::ui::views::ViewRef;
using fuchsia::ui::views::ViewRefInstalled_Watch_Result;
using scenic_impl::gfx::ViewRefInstalledImpl;

TEST(ViewRefInstalledImplTest, AlreadyInstalled_ShouldReturnImmediately) {
  async::TestLoop test_loop;

  ViewRefInstalledImpl view_ref_installed_impl(
      /*is_installed*/ [](zx_koid_t) { return true; });  // Always true.
  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();

  bool was_installed = false;
  view_ref_installed_impl.Watch(
      std::move(view_pair.view_ref),
      [&was_installed](ViewRefInstalled_Watch_Result result) { was_installed = !result.is_err(); });

  test_loop.RunUntilIdle();
  EXPECT_TRUE(was_installed);
}

TEST(ViewRefInstalledImplTest, ViewRefWithBadHandle_ShouldReturnErrorImmediately) {
  async::TestLoop test_loop;

  ViewRefInstalledImpl view_ref_installed_impl(
      /*is_installed*/ [](zx_koid_t) { return false; });  // Always false.

  // Create a not properly initialized ViewRefPair.
  scenic::ViewRefPair view_pair;

  bool was_error = false;
  view_ref_installed_impl.Watch(
      std::move(view_pair.view_ref),
      [&was_error](ViewRefInstalled_Watch_Result result) { was_error = result.is_err(); });
  test_loop.RunUntilIdle();
  EXPECT_TRUE(was_error);
}

TEST(ViewRefInstalledImplTest, ViewRefWithBadRights_ShouldReturnErrorImmediately) {
  async::TestLoop test_loop;

  ViewRefInstalledImpl view_ref_installed_impl(
      /*is_installed*/ [](zx_koid_t) { return false; });  // Always false.

  // Create a ViewRefPair where the ViewRef has faulty rights.
  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  auto status =
      view_pair.view_ref.reference.replace(ZX_RIGHT_INSPECT, &view_pair.view_ref.reference);
  ASSERT_EQ(status, ZX_OK);

  bool was_error = false;
  view_ref_installed_impl.Watch(
      std::move(view_pair.view_ref),
      [&was_error](ViewRefInstalled_Watch_Result result) { was_error = result.is_err(); });
  test_loop.RunUntilIdle();
  EXPECT_TRUE(was_error);
}

TEST(ViewRefInstalledImplTest, ViewRefWithClosedControlRef_ShouldReturnErrorImmediately) {
  async::TestLoop test_loop;

  ViewRefInstalledImpl view_ref_installed_impl(
      /*is_installed*/ [](zx_koid_t) { return false; });  // Always false.

  // Create a ViewRefPair and close the ViewRefControl before passing in the ViewRef.
  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  zx_handle_close(view_pair.control_ref.reference.get());

  bool was_error = false;
  view_ref_installed_impl.Watch(
      std::move(view_pair.view_ref),
      [&was_error](ViewRefInstalled_Watch_Result result) { was_error = result.is_err(); });
  test_loop.RunUntilIdle();
  EXPECT_TRUE(was_error);
}

TEST(ViewRefInstalledImplTest, OnViewRefInstalled_ShouldFireWaitingCallbacks) {
  async::TestLoop test_loop;

  ViewRefInstalledImpl view_ref_installed_impl(
      /*is_installed*/ [](zx_koid_t) { return false; });  // Always false.
  scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
  const zx_koid_t koid = utils::ExtractKoid(view_pair.view_ref);

  bool has_fired = false;
  bool was_error = false;
  view_ref_installed_impl.Watch(std::move(view_pair.view_ref),
                                [&has_fired, &was_error](ViewRefInstalled_Watch_Result result) {
                                  has_fired = true;
                                  was_error = result.is_err();
                                });
  test_loop.RunUntilIdle();
  EXPECT_FALSE(has_fired);

  view_ref_installed_impl.OnViewRefInstalled(koid);
  test_loop.RunUntilIdle();
  EXPECT_TRUE(has_fired);
  EXPECT_FALSE(was_error);
}

TEST(ViewRefInstalledImplTest, OnViewRefInvalidated_ShouldFireCallbackWithError) {
  async::TestLoop test_loop;
  async_set_default_dispatcher(test_loop.dispatcher());

  ViewRefInstalledImpl view_ref_installed_impl(
      /*is_installed*/ [](zx_koid_t) { return false; });  // Always false.

  bool has_fired = false;
  bool was_error = false;
  {
    scenic::ViewRefPair view_pair = scenic::ViewRefPair::New();
    view_ref_installed_impl.Watch(std::move(view_pair.view_ref),
                                  [&has_fired, &was_error](ViewRefInstalled_Watch_Result result) {
                                    has_fired = true;
                                    was_error = result.is_err();
                                  });
    test_loop.RunUntilIdle();
    EXPECT_FALSE(has_fired);
  }  // ViewRefControl goes out of scope, invalidating the passed in ViewRef.
  test_loop.RunUntilIdle();
  EXPECT_TRUE(has_fired);
  EXPECT_TRUE(was_error);
}

}  // namespace lib_ui_gfx_engine_tests
