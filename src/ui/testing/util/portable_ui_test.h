// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UTIL_PORTABLE_UI_TEST_H_
#define SRC_UI_TESTING_UTIL_PORTABLE_UI_TEST_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/ui/test/input/cpp/fidl.h>
#include <fuchsia/ui/test/scene/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <zircon/status.h>

#include <optional>
#include <vector>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace ui_testing {

class PortableUITest : public ::loop_fixture::RealLoop, public ::testing::Test {
 public:
  static constexpr auto kTestUIStack = "ui";
  static constexpr auto kTestUIStackRef = component_testing::ChildRef{kTestUIStack};

  void SetUp() override;

  // Attaches a client view to the scene, and waits for it to render.
  void LaunchClient();

  // Attaches a client view that embeds a child view to the scene, and waits for
  // both to render.
  void LaunchClientWithEmbeddedView();

  // Returns true when the specified view is fully connected to the scene AND
  // has presented at least one frame of content.
  bool HasViewConnected(zx_koid_t view_ref_koid);

  // Registers a fake touch screen device with an injection coordinate space
  // spanning [-1000, 1000] on both axes.
  void RegisterTouchScreen();

  // Simulates a tap at location (x, y).
  void InjectTap(int32_t x, int32_t y);

  // Injects an input event, and posts a task to retry after `kTapRetryInterval`.
  //
  // We post the retry task because the first input event we send to Flutter may be lost.
  // The reason the first event may be lost is that there is a race condition as the scene
  // owner starts up.
  //
  // More specifically: in order for our app
  // to receive the injected input, two things must be true before we inject touch input:
  // * The Scenic root view must have been installed, and
  // * The Input Pipeline must have received a viewport to inject touch into.
  //
  // The problem we have is that the `is_rendering` signal that we monitor only guarantees us
  // the view is ready. If the viewport is not ready in Input Pipeline at that time, it will
  // drop the touch event.
  //
  // TODO(fxbug.dev/96986): Improve synchronization and remove retry logic.
  void InjectTapWithRetry(int32_t x, int32_t y);

  // Injects a swipe from the given starting location to the given end location
  // in injector coordinate space.
  void InjectSwipe(int start_x, int start_y, int end_x, int end_y, int move_event_count);

  // Registers a fake mouse device, for which mouse movement is measured on a
  // scale of [-1000, 1000] on both axes and scroll is measured from [-100, 100]
  // on both axes.
  void RegisterMouse();

  // Helper method to simulate combinations of button presses/releases and/or
  // mouse movements.
  void SimulateMouseEvent(std::vector<fuchsia::ui::test::input::MouseButton> pressed_buttons,
                          int movement_x, int movement_y);

  // Helper method to simulate a mouse scroll event.
  //
  // Set `use_physical_units` to true to specify scroll in physical pixels and
  // false to specify scroll in detents.
  void SimulateMouseScroll(std::vector<fuchsia::ui::test::input::MouseButton> pressed_buttons,
                           int scroll_x, int scroll_y, bool use_physical_units = false);

 protected:
  component_testing::RealmBuilder* realm_builder() { return &realm_builder_; }
  component_testing::RealmRoot* realm_root() { return realm_.get(); }

  const std::optional<zx_koid_t>& client_root_view_ref_koid() { return client_root_view_ref_koid_; }

  int touch_injection_request_count() const { return touch_injection_request_count_; }

 private:
  void SetUpRealmBase();

  // Configures the test-specific component topology.
  virtual void ExtendRealm() = 0;

  // Returns the test-ui-stack component url to use in this test.
  virtual std::string GetTestUIStackUrl() = 0;

  // Helper method to watch watch for view geometry updates.
  void WatchViewGeometry();

  // Helper method to process a view geometry update.
  void ProcessViewGeometryResponse(fuchsia::ui::observation::geometry::WatchResponse response);

  fuchsia::ui::test::input::RegistryPtr input_registry_;
  fuchsia::ui::test::input::TouchScreenPtr fake_touchscreen_;
  fuchsia::ui::test::input::MousePtr fake_mouse_;
  fuchsia::ui::test::scene::ControllerPtr scene_provider_;
  fuchsia::ui::observation::geometry::ViewTreeWatcherPtr view_tree_watcher_;

  component_testing::RealmBuilder realm_builder_ = component_testing::RealmBuilder::Create();
  std::unique_ptr<component_testing::RealmRoot> realm_;

  // Counts the number of completed requests to inject touch reports into input
  // pipeline.
  int touch_injection_request_count_ = 0;

  // The KOID of the client root view's `ViewRef`.
  std::optional<zx_koid_t> client_root_view_ref_koid_;

  // Holds the most recent view tree snapshot received from the view tree
  // watcher.
  //
  // From this snapshot, we can retrieve relevant view tree state on demand,
  // e.g. if the client view is rendering content.
  std::optional<fuchsia::ui::observation::geometry::ViewTreeSnapshot> last_view_tree_snapshot_;

  // The typical latency on devices we've tested is ~60 msec. The retry interval is chosen to be
  // a) Long enough that it's unlikely that we send a new tap while a previous tap is still being
  //    processed. That is, it should be far more likely that a new tap is sent because the first
  //    tap was lost, than because the system is just running slowly.
  // b) Short enough that we don't slow down tryjobs.
  //
  // The first property is important to avoid skewing the latency metrics that we collect.
  // For an explanation of why a tap might be lost, see the documentation for TryInject().
  static constexpr auto kTapRetryInterval = zx::sec(1);
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UTIL_PORTABLE_UI_TEST_H_
