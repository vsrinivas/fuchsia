// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/view_tree/view_ref_installed_impl.h"

#include <lib/async-testing/test_loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/scenic/lib/utils/helpers.h"

namespace view_tree::test {

using fuchsia::ui::views::ViewRef;
using fuchsia::ui::views::ViewRefInstalled_Watch_Result;

TEST(ViewRefInstalledImplTest, AlreadyInstalled_ShouldReturnImmediately) {
  async::TestLoop test_loop;

  ViewRefInstalledImpl view_ref_installed_impl;

  auto [control_ref, view_ref] = scenic::ViewRefPair::New();
  const zx_koid_t koid = utils::ExtractKoid(view_ref);

  // Koid is in the ViewTree.
  auto snapshot = std::make_shared<Snapshot>();
  snapshot->view_tree[koid];
  view_ref_installed_impl.OnNewViewTreeSnapshot(snapshot);

  bool was_installed = false;
  view_ref_installed_impl.Watch(
      std::move(view_ref),
      [&was_installed](ViewRefInstalled_Watch_Result result) { was_installed = !result.is_err(); });

  test_loop.RunUntilIdle();
  EXPECT_TRUE(was_installed);
}

TEST(ViewRefInstalledImplTest, AlreadyInstalledButDisconnected_ShouldReturnImmediately) {
  async::TestLoop test_loop;

  ViewRefInstalledImpl view_ref_installed_impl;

  auto [control_ref, view_ref] = scenic::ViewRefPair::New();
  const zx_koid_t koid = utils::ExtractKoid(view_ref);

  {  // Koid is in the ViewTree.
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->view_tree[koid];
    view_ref_installed_impl.OnNewViewTreeSnapshot(snapshot);
  }

  {  // Koid is unconnected.
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->unconnected_views.emplace(koid);
    view_ref_installed_impl.OnNewViewTreeSnapshot(snapshot);
  }

  bool was_installed = false;
  view_ref_installed_impl.Watch(
      std::move(view_ref),
      [&was_installed](ViewRefInstalled_Watch_Result result) { was_installed = !result.is_err(); });

  test_loop.RunUntilIdle();
  EXPECT_TRUE(was_installed);
}

TEST(ViewRefInstalledImplTest, ViewRefWithBadHandle_ShouldReturnErrorImmediately) {
  async::TestLoop test_loop;

  ViewRefInstalledImpl view_ref_installed_impl;

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

  ViewRefInstalledImpl view_ref_installed_impl;

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

  ViewRefInstalledImpl view_ref_installed_impl;

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

  ViewRefInstalledImpl view_ref_installed_impl;
  auto [control_ref, view_ref] = scenic::ViewRefPair::New();
  const zx_koid_t koid = utils::ExtractKoid(view_ref);

  bool has_fired = false;
  bool was_error = false;
  view_ref_installed_impl.Watch(std::move(view_ref),
                                [&has_fired, &was_error](ViewRefInstalled_Watch_Result result) {
                                  has_fired = true;
                                  was_error = result.is_err();
                                });
  test_loop.RunUntilIdle();
  EXPECT_FALSE(has_fired);

  // Submit a new snapshot where the koid is in the ViewTree.
  auto snapshot = std::make_shared<Snapshot>();
  snapshot->view_tree[koid];
  view_ref_installed_impl.OnNewViewTreeSnapshot(snapshot);

  test_loop.RunUntilIdle();
  EXPECT_TRUE(has_fired);
  EXPECT_FALSE(was_error);
}

TEST(ViewRefInstalledImplTest, OnViewRefInvalidated_ShouldFireCallbackWithError) {
  async::TestLoop test_loop;
  async_set_default_dispatcher(test_loop.dispatcher());

  ViewRefInstalledImpl view_ref_installed_impl;

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

TEST(ViewRefInstalledImplTest, InstalledThenInvalidated) {
  async::TestLoop test_loop;

  ViewRefInstalledImpl view_ref_installed_impl;
  bool has_fired = false;
  bool was_error = false;

  {
    auto [control_ref, view_ref] = scenic::ViewRefPair::New();
    const zx_koid_t koid = utils::ExtractKoid(view_ref);

    view_ref_installed_impl.Watch(std::move(view_ref),
                                  [&has_fired, &was_error](ViewRefInstalled_Watch_Result result) {
                                    has_fired = true;
                                    was_error = result.is_err();
                                  });
    test_loop.RunUntilIdle();
    EXPECT_FALSE(has_fired);

    // Submit a new snapshot where the koid is in the ViewTree.
    async::PostTask(test_loop.dispatcher(), [&view_ref_installed_impl, koid] {
      auto snapshot = std::make_shared<Snapshot>();
      snapshot->view_tree[koid];
      view_ref_installed_impl.OnNewViewTreeSnapshot(snapshot);
    });
  }  // ViewRefControl goes out of scope, invalidating the passed in ViewRef.

  // Two things are now on the dispatch queue:
  // 1. OnNewViewTreeSnapshot(), which should trigger OnViewRefInstalled().
  // 2. ViewRef invalidation, which should trigger OnViewRefInvalidated().
  // Observe that this is handled gracefully.
  test_loop.RunUntilIdle();
  EXPECT_TRUE(has_fired);
  EXPECT_FALSE(was_error);
}

}  // namespace view_tree::test
