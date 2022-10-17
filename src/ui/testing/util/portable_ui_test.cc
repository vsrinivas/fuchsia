// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/testing/util/portable_ui_test.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>

namespace ui_testing {

namespace {

// Types imported for the realm_builder library.
using component_testing::ChildRef;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::RealmRoot;
using component_testing::Route;

bool CheckViewExistsInSnapshot(const fuchsia::ui::observation::geometry::ViewTreeSnapshot& snapshot,
                               zx_koid_t view_ref_koid) {
  if (!snapshot.has_views()) {
    return false;
  }

  auto snapshot_count = std::count_if(
      snapshot.views().begin(), snapshot.views().end(),
      [view_ref_koid](const auto& view) { return view.view_ref_koid() == view_ref_koid; });

  return snapshot_count > 0;
}

}  // namespace

void PortableUITest::SetUpRealmBase() {
  FX_LOGS(INFO) << "Setting up realm base.";

  // Add test UI stack component.
  realm_builder_.AddChild(kTestUIStack, GetTestUIStackUrl());

  // Route base system services to flutter and the test UI stack.
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                             Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                             Protocol{fuchsia::sys::Environment::Name_},
                             Protocol{fuchsia::sysmem::Allocator::Name_},
                             Protocol{fuchsia::vulkan::loader::Loader::Name_},
                             Protocol{fuchsia::tracing::provider::Registry::Name_}},
            .source = ParentRef{},
            .targets = {kTestUIStackRef}});

  // Capabilities routed to test driver.
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::ui::test::input::Registry::Name_},
                             Protocol{fuchsia::ui::test::scene::Controller::Name_}},
            .source = kTestUIStackRef,
            .targets = {ParentRef{}}});
}

void PortableUITest::SetUp() {
  SetUpRealmBase();

  ExtendRealm();

  realm_ = std::make_unique<RealmRoot>(realm_builder_.Build());
}

void PortableUITest::ProcessViewGeometryResponse(
    fuchsia::ui::observation::geometry::WatchResponse response) {
  // Process update if no error.
  if (!response.has_error()) {
    std::vector<fuchsia::ui::observation::geometry::ViewTreeSnapshot>* updates =
        response.mutable_updates();
    if (updates && !updates->empty()) {
      last_view_tree_snapshot_ = std::move(updates->back());
    }
  } else {
    // Otherwise, process error.
    const auto& error = response.error();

    if (error | fuchsia::ui::observation::geometry::Error::CHANNEL_OVERFLOW) {
      FX_LOGS(DEBUG) << "View Tree watcher channel overflowed";
    } else if (error | fuchsia::ui::observation::geometry::Error::BUFFER_OVERFLOW) {
      FX_LOGS(DEBUG) << "View Tree watcher buffer overflowed";
    } else if (error | fuchsia::ui::observation::geometry::Error::VIEWS_OVERFLOW) {
      // This one indicates some possible data loss, so we log with a high severity.
      FX_LOGS(WARNING) << "View Tree watcher attempted to report too many views";
    }
  }
}

void PortableUITest::WatchViewGeometry() {
  FX_CHECK(view_tree_watcher_) << "View Tree watcher must be registered before calling Watch()";

  view_tree_watcher_->Watch([this](auto response) {
    ProcessViewGeometryResponse(std::move(response));
    WatchViewGeometry();
  });
}

bool PortableUITest::HasViewConnected(zx_koid_t view_ref_koid) {
  return last_view_tree_snapshot_.has_value() &&
         CheckViewExistsInSnapshot(*last_view_tree_snapshot_, view_ref_koid);
}

void PortableUITest::LaunchClient() {
  scene_provider_ = realm_->Connect<fuchsia::ui::test::scene::Controller>();
  scene_provider_.set_error_handler(
      [](auto) { FX_LOGS(ERROR) << "Error from test scene provider"; });
  fuchsia::ui::test::scene::ControllerAttachClientViewRequest request;
  request.set_view_provider(realm_->Connect<fuchsia::ui::app::ViewProvider>());
  scene_provider_->RegisterViewTreeWatcher(view_tree_watcher_.NewRequest(), []() {});
  scene_provider_->AttachClientView(std::move(request), [this](auto client_view_ref_koid) {
    client_root_view_ref_koid_ = client_view_ref_koid;
  });

  FX_LOGS(INFO) << "Waiting for client view ref koid";
  RunLoopUntil([this] { return client_root_view_ref_koid_.has_value(); });

  WatchViewGeometry();

  FX_LOGS(INFO) << "Waiting for client view to connect";
  RunLoopUntil([this] { return HasViewConnected(*client_root_view_ref_koid_); });
  FX_LOGS(INFO) << "Client view has rendered";
}

void PortableUITest::LaunchClientWithEmbeddedView() {
  LaunchClient();

  // At this point, the parent view must have rendered, so we just need to wait
  // for the embedded view.
  RunLoopUntil([this] {
    if (!last_view_tree_snapshot_.has_value() || !last_view_tree_snapshot_->has_views()) {
      return false;
    }

    if (!client_root_view_ref_koid_.has_value()) {
      return false;
    }

    for (const auto& view : last_view_tree_snapshot_->views()) {
      if (!view.has_view_ref_koid() || view.view_ref_koid() != *client_root_view_ref_koid_) {
        continue;
      }

      if (view.children().empty()) {
        return false;
      }

      // NOTE: We can't rely on the presence of the child view in
      // `view.children()` to guarantee that it has rendered. The child view
      // also needs to be present in `last_view_tree_snapshot_->views`.
      return std::count_if(last_view_tree_snapshot_->views().begin(),
                           last_view_tree_snapshot_->views().end(),
                           [view_to_find = view.children().back()](const auto& view_to_check) {
                             return view_to_check.has_view_ref_koid() &&
                                    view_to_check.view_ref_koid() == view_to_find;
                           }) > 0;
    }

    return false;
  });

  FX_LOGS(INFO) << "Embedded view has rendered";
}

void PortableUITest::RegisterTouchScreen() {
  FX_LOGS(INFO) << "Registering fake touch screen";
  input_registry_ = realm_->Connect<fuchsia::ui::test::input::Registry>();
  input_registry_.set_error_handler([](auto) { FX_LOGS(ERROR) << "Error from input helper"; });

  bool touchscreen_registered = false;
  fuchsia::ui::test::input::RegistryRegisterTouchScreenRequest request;
  request.set_device(fake_touchscreen_.NewRequest());
  input_registry_->RegisterTouchScreen(
      std::move(request), [&touchscreen_registered]() { touchscreen_registered = true; });

  RunLoopUntil([&touchscreen_registered] { return touchscreen_registered; });
  FX_LOGS(INFO) << "Touchscreen registered";
}

void PortableUITest::InjectTap(int32_t x, int32_t y) {
  fuchsia::ui::test::input::TouchScreenSimulateTapRequest tap_request;
  tap_request.mutable_tap_location()->x = x;
  tap_request.mutable_tap_location()->y = y;

  FX_LOGS(INFO) << "Injecting tap at (" << tap_request.tap_location().x << ", "
                << tap_request.tap_location().y << ")";
  fake_touchscreen_->SimulateTap(std::move(tap_request), [this]() {
    ++touch_injection_request_count_;
    FX_LOGS(INFO) << "*** Tap injected, count: " << touch_injection_request_count_;
  });
}

void PortableUITest::InjectTapWithRetry(int32_t x, int32_t y) {
  InjectTap(x, y);
  async::PostDelayedTask(
      dispatcher(), [this, x, y] { InjectTapWithRetry(x, y); }, kTapRetryInterval);
}

void PortableUITest::InjectSwipe(int start_x, int start_y, int end_x, int end_y,
                                 int move_event_count) {
  fuchsia::ui::test::input::TouchScreenSimulateSwipeRequest swipe_request;
  swipe_request.mutable_start_location()->x = start_x;
  swipe_request.mutable_start_location()->y = start_y;
  swipe_request.mutable_end_location()->x = end_x;
  swipe_request.mutable_end_location()->y = end_y;
  swipe_request.set_move_event_count(move_event_count);

  FX_LOGS(INFO) << "Injecting swipe from (" << swipe_request.start_location().x << ", "
                << swipe_request.start_location().y << ") to (" << swipe_request.end_location().x
                << ", " << swipe_request.end_location().y
                << ") with move_event_count = " << swipe_request.move_event_count();

  fake_touchscreen_->SimulateSwipe(std::move(swipe_request), [this]() {
    touch_injection_request_count_++;
    FX_LOGS(INFO) << "*** Swipe injected";
  });
}

void PortableUITest::RegisterMouse() {
  FX_LOGS(INFO) << "Registering fake mouse";
  input_registry_ = realm_->Connect<fuchsia::ui::test::input::Registry>();
  input_registry_.set_error_handler([](auto) { FX_LOGS(ERROR) << "Error from input helper"; });

  bool mouse_registered = false;
  fuchsia::ui::test::input::RegistryRegisterMouseRequest request;
  request.set_device(fake_mouse_.NewRequest());
  input_registry_->RegisterMouse(std::move(request),
                                 [&mouse_registered]() { mouse_registered = true; });

  RunLoopUntil([&mouse_registered] { return mouse_registered; });
  FX_LOGS(INFO) << "Mouse registered";
}

void PortableUITest::SimulateMouseEvent(
    std::vector<fuchsia::ui::test::input::MouseButton> pressed_buttons, int movement_x,
    int movement_y) {
  FX_LOGS(INFO) << "Requesting mouse event";
  fuchsia::ui::test::input::MouseSimulateMouseEventRequest request;
  request.set_pressed_buttons(std::move(pressed_buttons));
  request.set_movement_x(movement_x);
  request.set_movement_y(movement_y);

  fake_mouse_->SimulateMouseEvent(std::move(request),
                                  [] { FX_LOGS(INFO) << "Mouse event injected"; });
}

void PortableUITest::SimulateMouseScroll(
    std::vector<fuchsia::ui::test::input::MouseButton> pressed_buttons, int scroll_x, int scroll_y,
    bool use_physical_units) {
  FX_LOGS(INFO) << "Requesting mouse scroll";
  fuchsia::ui::test::input::MouseSimulateMouseEventRequest request;
  request.set_pressed_buttons(std::move(pressed_buttons));
  if (use_physical_units) {
    request.set_scroll_h_physical_pixel(scroll_x);
    request.set_scroll_v_physical_pixel(scroll_y);
  } else {
    request.set_scroll_h_detent(scroll_x);
    request.set_scroll_v_detent(scroll_y);
  }

  fake_mouse_->SimulateMouseEvent(std::move(request),
                                  [] { FX_LOGS(INFO) << "Mouse scroll event injected"; });
}

}  // namespace ui_testing
