// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/input/report/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/pointer/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <lib/async-loop/testing/cpp/real_loop.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <zircon/status.h>

#include <zxtest/zxtest.h>

#include "src/ui/scenic/lib/utils/helpers.h"
#include "src/ui/scenic/tests/utils/scenic_realm_builder.h"

// These tests exercise the integration between Flatland and the InputSystem, including the
// View-to-View transform logic between the injection point and the receiver.
// Setup:
// - The test fixture sets up the display + the root session and view.
// - Injection done in context View Space, with fuchsia.ui.pointerinjector
// - Target(s) specified by View (using view ref koids)
// - Dispatch done to fuchsia.ui.pointer.MouseSource in receiver View Space.
namespace integration_tests {

using fuc_ParentViewportWatcher = fuchsia::ui::composition::ParentViewportWatcher;
using fuc_ChildViewWatcher = fuchsia::ui::composition::ChildViewWatcher;
using fuc_Flatland = fuchsia::ui::composition::Flatland;
using fuc_FlatlandPtr = fuchsia::ui::composition::FlatlandPtr;
using fuc_ViewBoundProtocols = fuchsia::ui::composition::ViewBoundProtocols;
using fuc_FlatlandDisplay = fuchsia::ui::composition::FlatlandDisplay;
using fuc_FlatlandDisplayPtr = fuchsia::ui::composition::FlatlandDisplayPtr;
using fuc_ViewportProperties = fuchsia::ui::composition::ViewportProperties;
using fuc_TransformId = fuchsia::ui::composition::TransformId;
using fuc_ContentId = fuchsia::ui::composition::ContentId;
using fuv_ViewRefFocused = fuchsia::ui::views::ViewRefFocused;
using fuv_ViewRefFocusedPtr = fuchsia::ui::views::ViewRefFocusedPtr;
using fuv_ViewRef = fuchsia::ui::views::ViewRef;
using fuv_FocusState = fuchsia::ui::views::FocusState;
using fup_MouseEvent = fuchsia::ui::pointer::MouseEvent;
using fup_MouseSource = fuchsia::ui::pointer::MouseSource;
using fup_MouseSourcePtr = fuchsia::ui::pointer::MouseSourcePtr;
using fupi_Config = fuchsia::ui::pointerinjector::Config;
using fupi_DispatchPolicy = fuchsia::ui::pointerinjector::DispatchPolicy;
using fupi_Event = fuchsia::ui::pointerinjector::Event;
using fupi_EventPhase = fuchsia::ui::pointerinjector::EventPhase;
using fupi_PointerSample = fuchsia::ui::pointerinjector::PointerSample;
using fupi_Context = fuchsia::ui::pointerinjector::Context;
using fupi_Data = fuchsia::ui::pointerinjector::Data;
using fupi_Registry = fuchsia::ui::pointerinjector::Registry;
using fupi_RegistryPtr = fuchsia::ui::pointerinjector::RegistryPtr;
using fupi_DevicePtr = fuchsia::ui::pointerinjector::DevicePtr;
using fupi_DeviceType = fuchsia::ui::pointerinjector::DeviceType;
using fupi_Target = fuchsia::ui::pointerinjector::Target;
using fupi_Viewport = fuchsia::ui::pointerinjector::Viewport;
using RealmRoot = component_testing::RealmRoot;
using fir_Axis = fuchsia::input::report::Axis;

class FlatlandMouseIntegrationTest : public zxtest::Test, public loop_fixture::RealLoop {
 protected:
  static constexpr uint32_t kDeviceId = 1111;

  static constexpr uint32_t kPointerId = 2222;

  static constexpr uint32_t kDefaultSize = 10;

  static constexpr fuc_TransformId kDefaultRootTransform = {.value = 1};

  // clang-format off
  static constexpr std::array<float, 9> kIdentityMatrix = {
    1, 0, 0, // column one
    0, 1, 0, // column two
    0, 0, 1, // column three
  };
  // clang-format on

  void SetUp() override {
    // Build the realm topology and route the protocols required by this test fixture from the
    // scenic subrealm.
    realm_ = std::make_unique<RealmRoot>(
        ScenicRealmBuilder()
            .AddRealmProtocol(fuchsia::ui::composition::Flatland::Name_)
            .AddRealmProtocol(fuchsia::ui::composition::FlatlandDisplay::Name_)
            .AddRealmProtocol(fuchsia::ui::composition::Allocator::Name_)
            .AddRealmProtocol(fuchsia::ui::pointerinjector::Registry::Name_)
            .Build());

    flatland_display_ = realm_->Connect<fuc_FlatlandDisplay>();
    flatland_display_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
    });

    pointerinjector_registry_ = realm_->Connect<fupi_Registry>();
    pointerinjector_registry_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to pointerinjector Registry: %s", zx_status_get_string(status));
    });

    // Set up root view.
    root_session_ = realm_->Connect<fuc_Flatland>();
    root_session_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
    });

    fidl::InterfacePtr<fuc_ChildViewWatcher> child_view_watcher;
    fuc_ViewBoundProtocols protocols;
    fuv_ViewRefFocusedPtr root_focused_ptr;

    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    fidl::InterfacePtr<fuc_ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    root_view_ref_ = fidl::Clone(identity.view_ref);
    protocols.set_view_ref_focused(root_focused_ptr.NewRequest());

    root_session_->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                               parent_viewport_watcher.NewRequest());

    parent_viewport_watcher->GetLayout([this](auto layout_info) {
      ASSERT_TRUE(layout_info.has_logical_size());
      const auto [width, height] = layout_info.logical_size();
      display_width_ = static_cast<float>(width);
      display_height_ = static_cast<float>(height);
    });

    flatland_display_->SetContent(std::move(parent_token), child_view_watcher.NewRequest());
    BlockingPresent(root_session_);

    // Wait until we get the display size.
    RunLoopUntil([this] { return display_width_ != 0 && display_height_ != 0; });
  }

  void BlockingPresent(fuc_FlatlandPtr& flatland) {
    bool presented = false;
    flatland.events().OnFramePresented = [&presented](auto) { presented = true; };
    flatland->Present({});
    RunLoopUntil([&presented] { return presented; });
    flatland.events().OnFramePresented = nullptr;
  }

  void Inject(float x, float y, fupi_EventPhase phase, std::vector<uint8_t> pressed_buttons = {},
              std::optional<int64_t> scroll_v = std::nullopt,
              std::optional<int64_t> scroll_h = std::nullopt,
              std::optional<double> scroll_v_physical_pixel = std::nullopt,
              std::optional<double> scroll_h_physical_pixel = std::nullopt,
              std::optional<bool> is_precision_scroll = std::nullopt) {
    FX_DCHECK(injector_);
    fupi_Event event;
    event.set_timestamp(0);
    {
      fupi_PointerSample pointer_sample;
      pointer_sample.set_pointer_id(kPointerId);
      pointer_sample.set_phase(phase);
      pointer_sample.set_position_in_viewport({x, y});
      if (scroll_v.has_value()) {
        pointer_sample.set_scroll_v(scroll_v.value());
      }
      if (scroll_h.has_value()) {
        pointer_sample.set_scroll_h(scroll_h.value());
      }
      if (scroll_v_physical_pixel.has_value()) {
        pointer_sample.set_scroll_v_physical_pixel(scroll_v_physical_pixel.value());
      }
      if (scroll_h_physical_pixel.has_value()) {
        pointer_sample.set_scroll_h_physical_pixel(scroll_h_physical_pixel.value());
      }
      if (is_precision_scroll.has_value()) {
        pointer_sample.set_is_precision_scroll(is_precision_scroll.value());
      }

      if (!pressed_buttons.empty()) {
        pointer_sample.set_pressed_buttons(pressed_buttons);
      }
      fupi_Data data;
      data.set_pointer_sample(std::move(pointer_sample));
      event.set_data(std::move(data));
    }
    std::vector<fupi_Event> events;
    events.emplace_back(std::move(event));
    injector_->Inject(std::move(events), [] {});
  }

  void RegisterInjector(fuv_ViewRef context_view_ref, fuv_ViewRef target_view_ref,
                        fupi_DispatchPolicy dispatch_policy, std::vector<uint8_t> buttons,
                        std::array<float, 9> viewport_to_context_transform) {
    fupi_Config config;
    config.set_device_id(kDeviceId);
    config.set_device_type(fupi_DeviceType::MOUSE);
    config.set_dispatch_policy(dispatch_policy);

    {
      fir_Axis axis;
      axis.range.min = -1;
      axis.range.max = 1;
      config.set_scroll_v_range(axis);
    }

    {
      fir_Axis axis;
      axis.range.min = -1;
      axis.range.max = 1;
      config.set_scroll_h_range(axis);
    }

    config.set_buttons(buttons);
    {
      {
        fupi_Context context;
        context.set_view(std::move(context_view_ref));
        config.set_context(std::move(context));
      }
      {
        fupi_Target target;
        target.set_view(std::move(target_view_ref));
        config.set_target(std::move(target));
      }
      {
        fupi_Viewport viewport;
        viewport.set_extents(FullScreenExtents());
        viewport.set_viewport_to_context_transform(viewport_to_context_transform);
        config.set_viewport(std::move(viewport));
      }
    }

    injector_.set_error_handler([this](zx_status_t) { injector_channel_closed_ = true; });
    bool register_callback_fired = false;
    pointerinjector_registry_->Register(
        std::move(config), injector_.NewRequest(),
        [&register_callback_fired] { register_callback_fired = true; });

    RunLoopUntil([&register_callback_fired] { return register_callback_fired; });
    EXPECT_FALSE(injector_channel_closed_);
  }

  // Starts a recursive MouseSource::Watch() loop that collects all received events into
  // |out_events|.
  void StartWatchLoop(fup_MouseSourcePtr& mouse_source, std::vector<fup_MouseEvent>& out_events) {
    const size_t index = watch_loops_.size();
    watch_loops_.emplace_back();
    watch_loops_.at(index) = [this, &mouse_source, &out_events,
                              index](std::vector<fup_MouseEvent> events) {
      std::move(events.begin(), events.end(), std::back_inserter(out_events));
      mouse_source->Watch([this, index](std::vector<fup_MouseEvent> events) {
        watch_loops_.at(index)(std::move(events));
      });
    };
    mouse_source->Watch(watch_loops_.at(index));
  }

  std::array<std::array<float, 2>, 2> FullScreenExtents() const {
    return {{{0, 0}, {display_width_, display_height_}}};
  }

  fuv_ViewRef CreateChildView(
      fuc_FlatlandPtr& child_session,
      fidl::InterfaceRequest<fup_MouseSource> child_mouse_source = nullptr,
      fidl::InterfaceRequest<fuv_ViewRefFocused> child_focused_ptr = nullptr) {
    root_session_->CreateTransform(kDefaultRootTransform);
    root_session_->SetRootTransform(kDefaultRootTransform);
    return CreateAndAddChildView(root_session_,
                                 /*viewport_transform*/ {.value = kDefaultRootTransform.value + 1},
                                 /*parent_of_viewport_transform*/ kDefaultRootTransform,
                                 /*parent_content*/ {.value = 1}, child_session,
                                 std::move(child_mouse_source), std::move(child_focused_ptr));
  }

  // This function assumes the parent_session was created via |CreateChildView()|. This assumption
  // means that the transform topology is a root transform with one level of N children. This
  // enables virtually every hit testing scenario with minimal test complexity.
  //
  // Prereq: |parent_of_viewport_transform| is created and connected to the view's root.
  fuv_ViewRef CreateAndAddChildView(
      fuc_FlatlandPtr& parent_session, fuc_TransformId viewport_transform,
      fuc_TransformId parent_of_viewport_transform, fuc_ContentId parent_content,
      fuc_FlatlandPtr& child_session,
      fidl::InterfaceRequest<fup_MouseSource> child_mouse_source = nullptr,
      fidl::InterfaceRequest<fuv_ViewRefFocused> child_focused_ptr = nullptr) {
    child_session = realm_->Connect<fuc_Flatland>();

    // Set up the child view watcher.
    fidl::InterfacePtr<fuc_ChildViewWatcher> child_view_watcher;
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    fuc_ViewportProperties properties;
    properties.set_logical_size({kDefaultSize, kDefaultSize});

    parent_session->CreateTransform(viewport_transform);
    parent_session->CreateViewport(parent_content, std::move(parent_token), std::move(properties),
                                   child_view_watcher.NewRequest());
    parent_session->SetContent(viewport_transform, parent_content);
    parent_session->AddChild(parent_of_viewport_transform, viewport_transform);

    BlockingPresent(parent_session);

    // Set up the child view along with its MouseSource and ViewRefFocused channel.
    fidl::InterfacePtr<fuc_ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    auto child_view_ref = fidl::Clone(identity.view_ref);
    fuc_ViewBoundProtocols protocols;
    if (child_mouse_source)
      protocols.set_mouse_source(std::move(child_mouse_source));
    if (child_focused_ptr)
      protocols.set_view_ref_focused(std::move(child_focused_ptr));
    child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                               parent_viewport_watcher.NewRequest());
    child_session->CreateTransform(kDefaultRootTransform);
    child_session->SetRootTransform(kDefaultRootTransform);
    BlockingPresent(child_session);

    return child_view_ref;
  }

  fuc_FlatlandPtr root_session_;

  fuv_ViewRef root_view_ref_;

  bool injector_channel_closed_ = false;

  float display_width_ = 0;

  float display_height_ = 0;

  std::unique_ptr<RealmRoot> realm_;

 private:
  fuc_FlatlandDisplayPtr flatland_display_;

  fupi_RegistryPtr pointerinjector_registry_;

  fupi_DevicePtr injector_;

  // Holds watch loops so they stay alive through the duration of the test.
  std::vector<std::function<void(std::vector<fup_MouseEvent>)>> watch_loops_;
};

// The child view should receive focus and input events when the mouse button is pressed over its
// view.
TEST_F(FlatlandMouseIntegrationTest, ChildReceivesFocus_OnMouseLatch) {
  fuc_FlatlandPtr child_session;
  fup_MouseSourcePtr child_mouse_source;
  fuv_ViewRefFocusedPtr child_focused_ptr;

  child_session.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  child_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  child_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_session, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<fup_MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Inject an input event at (0,0) which is the point of overlap between the parent and the
  // child.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   fupi_DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec,
                   kIdentityMatrix);
  Inject(0, 0, fupi_EventPhase::ADD, button_vec);

  // Child should receive mouse input events.
  RunLoopUntil([&child_events] { return child_events.size() == 1u; });

  // Child view should receive focus.
  std::optional<fuv_FocusState> child_focused;
  child_focused_ptr->Watch([&child_focused](auto update) { child_focused = std::move(update); });
  RunLoopUntil([&child_focused] { return child_focused.has_value(); });
  EXPECT_TRUE(child_focused->focused());
}

// Send wheel events to scenic ensure client receives wheel events.
TEST_F(FlatlandMouseIntegrationTest, Wheel) {
  fuc_FlatlandPtr child_session;
  fup_MouseSourcePtr child_mouse_source;
  fuv_ViewRefFocusedPtr child_focused_ptr;

  child_session.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  child_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  child_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_session, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<fup_MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Inject an input event at (0,0) which is the point of overlap between the parent and the
  // child.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   fupi_DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec,
                   kIdentityMatrix);
  Inject(0, 0, fupi_EventPhase::ADD, button_vec);
  Inject(0, 0, fupi_EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::optional<int64_t>(1), /* scroll_h= */ std::optional<int64_t>(-1));

  RunLoopUntil([&child_events] { return child_events.size() == 2u; });

  ASSERT_TRUE(child_events[0].has_pointer_sample());
  EXPECT_FALSE(child_events[0].pointer_sample().has_scroll_v());
  EXPECT_FALSE(child_events[0].pointer_sample().has_scroll_h());
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_v_physical_pixel());
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_h_physical_pixel());
  ASSERT_FALSE(child_events[0].pointer_sample().has_is_precision_scroll());

  ASSERT_TRUE(child_events[1].has_pointer_sample());
  ASSERT_TRUE(child_events[1].pointer_sample().has_scroll_v());
  EXPECT_EQ(child_events[1].pointer_sample().scroll_v(), 1);
  ASSERT_TRUE(child_events[1].pointer_sample().has_scroll_h());
  EXPECT_EQ(child_events[1].pointer_sample().scroll_h(), -1);
  ASSERT_FALSE(child_events[1].pointer_sample().has_scroll_v_physical_pixel());
  ASSERT_FALSE(child_events[1].pointer_sample().has_scroll_h_physical_pixel());
  ASSERT_FALSE(child_events[1].pointer_sample().has_is_precision_scroll());
}

// Send wheel events in button pressing sequence to scenic ensure client receives correct wheel
// events.
TEST_F(FlatlandMouseIntegrationTest, DownWheelUpWheel) {
  fuc_FlatlandPtr child_session;
  fup_MouseSourcePtr child_mouse_source;
  fuv_ViewRefFocusedPtr child_focused_ptr;

  child_session.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  child_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  child_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_session, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<fup_MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Inject an input event at (0,0) which is the point of overlap between the parent and the
  // child.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   fupi_DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec,
                   kIdentityMatrix);
  Inject(0, 0, fupi_EventPhase::ADD, button_vec);
  Inject(0, 0, fupi_EventPhase::CHANGE, button_vec);
  Inject(0, 0, fupi_EventPhase::CHANGE, button_vec,
         /* scroll_v= */ std::optional<int64_t>(1));
  Inject(0, 0, fupi_EventPhase::CHANGE, /* pressed_buttons= */ {});
  Inject(0, 0, fupi_EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::optional<int64_t>(1));

  RunLoopUntil([&child_events] { return child_events.size() == 5u; });

  ASSERT_TRUE(child_events[0].has_pointer_sample());
  EXPECT_FALSE(child_events[0].pointer_sample().has_scroll_v());
  EXPECT_FALSE(child_events[0].pointer_sample().has_scroll_h());
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_v_physical_pixel());
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_h_physical_pixel());
  ASSERT_FALSE(child_events[0].pointer_sample().has_is_precision_scroll());

  ASSERT_TRUE(child_events[1].has_pointer_sample());
  EXPECT_EQ(child_events[1].pointer_sample().pressed_buttons(), button_vec);

  ASSERT_TRUE(child_events[2].has_pointer_sample());
  ASSERT_TRUE(child_events[2].pointer_sample().has_scroll_v());
  EXPECT_EQ(child_events[2].pointer_sample().scroll_v(), 1);
  EXPECT_FALSE(child_events[2].pointer_sample().has_scroll_h());
  EXPECT_EQ(child_events[2].pointer_sample().pressed_buttons(), button_vec);
  EXPECT_FALSE(child_events[2].pointer_sample().has_is_precision_scroll());
  ASSERT_FALSE(child_events[2].pointer_sample().has_scroll_v_physical_pixel());
  ASSERT_FALSE(child_events[2].pointer_sample().has_scroll_h_physical_pixel());
  ASSERT_FALSE(child_events[2].pointer_sample().has_is_precision_scroll());

  ASSERT_TRUE(child_events[3].has_pointer_sample());
  EXPECT_FALSE(child_events[3].pointer_sample().has_pressed_buttons());
  EXPECT_FALSE(child_events[3].pointer_sample().has_scroll_v());
  EXPECT_FALSE(child_events[3].pointer_sample().has_is_precision_scroll());
  ASSERT_FALSE(child_events[3].pointer_sample().has_scroll_v_physical_pixel());
  ASSERT_FALSE(child_events[3].pointer_sample().has_scroll_h_physical_pixel());
  ASSERT_FALSE(child_events[3].pointer_sample().has_is_precision_scroll());

  ASSERT_TRUE(child_events[4].has_pointer_sample());
  ASSERT_TRUE(child_events[4].pointer_sample().has_scroll_v());
  EXPECT_EQ(child_events[4].pointer_sample().scroll_v(), 1);
  EXPECT_FALSE(child_events[4].pointer_sample().has_scroll_h());
  EXPECT_FALSE(child_events[4].pointer_sample().has_pressed_buttons());
  ASSERT_FALSE(child_events[4].pointer_sample().has_scroll_v_physical_pixel());
  ASSERT_FALSE(child_events[4].pointer_sample().has_scroll_h_physical_pixel());
  ASSERT_FALSE(child_events[4].pointer_sample().has_is_precision_scroll());
}

// Send wheel events bundled with button changess to scenic ensure client receives correct wheel
// events.
TEST_F(FlatlandMouseIntegrationTest, DownWheelUpWheelBundled) {
  fuc_FlatlandPtr child_session;
  fup_MouseSourcePtr child_mouse_source;
  fuv_ViewRefFocusedPtr child_focused_ptr;

  child_session.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  child_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  child_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_session, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<fup_MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Inject an input event at (0,0) which is the point of overlap between the parent and the
  // child.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   fupi_DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec,
                   kIdentityMatrix);
  Inject(0, 0, fupi_EventPhase::ADD, button_vec);
  // This event bundled button down and wheel.
  Inject(0, 0, fupi_EventPhase::CHANGE, button_vec, /* scroll_v= */ std::optional<int64_t>(1));
  Inject(0, 0, fupi_EventPhase::CHANGE, button_vec, /* scroll_v= */ std::optional<int64_t>(1));
  // This event bundled button up and wheel.
  Inject(0, 0, fupi_EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::optional<int64_t>(1));
  Inject(0, 0, fupi_EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::optional<int64_t>(1));

  RunLoopUntil([&child_events] { return child_events.size() == 5u; });

  ASSERT_TRUE(child_events[0].has_pointer_sample());
  EXPECT_FALSE(child_events[0].pointer_sample().has_scroll_v());
  EXPECT_FALSE(child_events[0].pointer_sample().has_scroll_h());
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_v_physical_pixel());
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_h_physical_pixel());
  ASSERT_FALSE(child_events[0].pointer_sample().has_is_precision_scroll());

  ASSERT_TRUE(child_events[1].has_pointer_sample());
  ASSERT_TRUE(child_events[1].pointer_sample().has_scroll_v());
  EXPECT_EQ(child_events[1].pointer_sample().scroll_v(), 1);
  EXPECT_EQ(child_events[1].pointer_sample().pressed_buttons(), button_vec);
  ASSERT_FALSE(child_events[1].pointer_sample().has_scroll_v_physical_pixel());
  ASSERT_FALSE(child_events[1].pointer_sample().has_scroll_h_physical_pixel());
  ASSERT_FALSE(child_events[1].pointer_sample().has_is_precision_scroll());

  ASSERT_TRUE(child_events[2].has_pointer_sample());
  ASSERT_TRUE(child_events[2].pointer_sample().has_scroll_v());
  EXPECT_EQ(child_events[2].pointer_sample().scroll_v(), 1);
  EXPECT_EQ(child_events[2].pointer_sample().pressed_buttons(), button_vec);
  ASSERT_FALSE(child_events[2].pointer_sample().has_scroll_v_physical_pixel());
  ASSERT_FALSE(child_events[2].pointer_sample().has_scroll_h_physical_pixel());
  ASSERT_FALSE(child_events[2].pointer_sample().has_is_precision_scroll());

  ASSERT_TRUE(child_events[3].has_pointer_sample());
  ASSERT_TRUE(child_events[3].pointer_sample().has_scroll_v());
  EXPECT_EQ(child_events[3].pointer_sample().scroll_v(), 1);
  EXPECT_FALSE(child_events[3].pointer_sample().has_pressed_buttons());
  ASSERT_FALSE(child_events[3].pointer_sample().has_scroll_v_physical_pixel());
  ASSERT_FALSE(child_events[3].pointer_sample().has_scroll_h_physical_pixel());
  ASSERT_FALSE(child_events[3].pointer_sample().has_is_precision_scroll());

  ASSERT_TRUE(child_events[4].has_pointer_sample());
  ASSERT_TRUE(child_events[4].pointer_sample().has_scroll_v());
  EXPECT_EQ(child_events[4].pointer_sample().scroll_v(), 1);
  EXPECT_FALSE(child_events[4].pointer_sample().has_pressed_buttons());
  ASSERT_FALSE(child_events[4].pointer_sample().has_scroll_v_physical_pixel());
  ASSERT_FALSE(child_events[4].pointer_sample().has_scroll_h_physical_pixel());
  ASSERT_FALSE(child_events[4].pointer_sample().has_is_precision_scroll());
}

// Send wheel events with physical pixel fields to scenic ensure client receives wheel events.
TEST_F(FlatlandMouseIntegrationTest, WheelWithPhysicalPixel) {
  fuc_FlatlandPtr child_session;
  fup_MouseSourcePtr child_mouse_source;
  fuv_ViewRefFocusedPtr child_focused_ptr;

  child_session.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  child_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  child_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_session, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<fup_MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Inject an input event at (0,0) which is the point of overlap between the parent and the
  // child.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   fupi_DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec,
                   kIdentityMatrix);
  Inject(0, 0, fupi_EventPhase::ADD, button_vec);

  RunLoopUntil([&child_events] { return child_events.size() == 1u; });
  ASSERT_TRUE(child_events[0].has_pointer_sample());
  EXPECT_FALSE(child_events[0].pointer_sample().has_scroll_v());
  EXPECT_FALSE(child_events[0].pointer_sample().has_scroll_h());
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_v_physical_pixel());
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_h_physical_pixel());
  ASSERT_FALSE(child_events[0].pointer_sample().has_is_precision_scroll());
  child_events.clear();

  // with v physical pixel, not precision scroll
  Inject(0, 0, fupi_EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::optional<int64_t>(1),
         /* scroll_h= */ std::nullopt,
         /* scroll_v_physical_pixel= */ std::optional<double>(120.0),
         /* scroll_h_physical_pixel= */ std::nullopt,
         /* is_precision_scroll= */ std::optional<bool>(false));

  // with h physical pixel, not precision scroll
  Inject(0, 0, fupi_EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::nullopt,
         /* scroll_h= */ std::optional<int64_t>(-1),
         /* scroll_v_physical_pixel= */ std::nullopt,
         /* scroll_h_physical_pixel= */ std::optional<double>(-120.0),
         /* is_precision_scroll= */ std::optional<bool>(false));

  // with v,h physical pixel, not precision scroll
  Inject(0, 0, fupi_EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::optional<int64_t>(1),
         /* scroll_h= */ std::optional<int64_t>(-1),
         /* scroll_v_physical_pixel= */ std::optional<double>(120.0),
         /* scroll_h_physical_pixel= */ std::optional<double>(-120.0),
         /* is_precision_scroll= */ std::optional<bool>(false));

  RunLoopUntil([&child_events] { return child_events.size() == 3u; });

  ASSERT_TRUE(child_events[0].has_pointer_sample());
  ASSERT_TRUE(child_events[0].pointer_sample().has_scroll_v());
  EXPECT_EQ(child_events[0].pointer_sample().scroll_v(), 1);
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_h());
  ASSERT_TRUE(child_events[0].pointer_sample().has_scroll_v_physical_pixel());
  EXPECT_EQ(child_events[0].pointer_sample().scroll_v_physical_pixel(), 120.0);
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_h_physical_pixel());
  ASSERT_TRUE(child_events[0].pointer_sample().has_is_precision_scroll());
  EXPECT_FALSE(child_events[0].pointer_sample().is_precision_scroll());

  ASSERT_TRUE(child_events[1].has_pointer_sample());
  ASSERT_FALSE(child_events[1].pointer_sample().has_scroll_v());
  ASSERT_TRUE(child_events[1].pointer_sample().has_scroll_h());
  EXPECT_EQ(child_events[1].pointer_sample().scroll_h(), -1);
  ASSERT_FALSE(child_events[1].pointer_sample().has_scroll_v_physical_pixel());
  ASSERT_TRUE(child_events[1].pointer_sample().has_scroll_h_physical_pixel());
  EXPECT_EQ(child_events[1].pointer_sample().scroll_h_physical_pixel(), -120.0);
  ASSERT_TRUE(child_events[1].pointer_sample().has_is_precision_scroll());
  EXPECT_FALSE(child_events[1].pointer_sample().is_precision_scroll());

  ASSERT_TRUE(child_events[2].has_pointer_sample());
  ASSERT_TRUE(child_events[2].pointer_sample().has_scroll_v());
  EXPECT_EQ(child_events[2].pointer_sample().scroll_v(), 1);
  ASSERT_TRUE(child_events[2].pointer_sample().has_scroll_h());
  EXPECT_EQ(child_events[2].pointer_sample().scroll_h(), -1);
  ASSERT_TRUE(child_events[2].pointer_sample().has_scroll_v_physical_pixel());
  EXPECT_EQ(child_events[2].pointer_sample().scroll_v_physical_pixel(), 120.0);
  ASSERT_TRUE(child_events[2].pointer_sample().has_scroll_h_physical_pixel());
  EXPECT_EQ(child_events[2].pointer_sample().scroll_h_physical_pixel(), -120.0);
  ASSERT_TRUE(child_events[2].pointer_sample().has_is_precision_scroll());
  EXPECT_FALSE(child_events[2].pointer_sample().is_precision_scroll());

  child_events.clear();

  // with v physical pixel, is precision scroll
  Inject(0, 0, fupi_EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::optional<int64_t>(1),
         /* scroll_h= */ std::nullopt,
         /* scroll_v_physical_pixel= */ std::optional<double>(120.0),
         /* scroll_h_physical_pixel= */ std::nullopt,
         /* is_precision_scroll= */ std::optional<bool>(true));

  // with h physical pixel, is precision scroll
  Inject(0, 0, fupi_EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::nullopt,
         /* scroll_h= */ std::optional<int64_t>(-1),
         /* scroll_v_physical_pixel= */ std::nullopt,
         /* scroll_h_physical_pixel= */ std::optional<double>(-120.0),
         /* is_precision_scroll= */ std::optional<bool>(true));

  // with v,h physical pixel, is precision scroll
  Inject(0, 0, fupi_EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::optional<int64_t>(1),
         /* scroll_h= */ std::optional<int64_t>(-1),
         /* scroll_v_physical_pixel= */ std::optional<double>(120.0),
         /* scroll_h_physical_pixel= */ std::optional<double>(-120.0),
         /* is_precision_scroll= */ std::optional<bool>(true));

  RunLoopUntil([&child_events] { return child_events.size() == 3u; });

  ASSERT_TRUE(child_events[0].has_pointer_sample());
  ASSERT_TRUE(child_events[0].pointer_sample().has_scroll_v());
  EXPECT_EQ(child_events[0].pointer_sample().scroll_v(), 1);
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_h());
  ASSERT_TRUE(child_events[0].pointer_sample().has_scroll_v_physical_pixel());
  EXPECT_EQ(child_events[0].pointer_sample().scroll_v_physical_pixel(), 120.0);
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_h_physical_pixel());
  ASSERT_TRUE(child_events[0].pointer_sample().has_is_precision_scroll());
  EXPECT_TRUE(child_events[0].pointer_sample().is_precision_scroll());

  ASSERT_TRUE(child_events[1].has_pointer_sample());
  ASSERT_FALSE(child_events[1].pointer_sample().has_scroll_v());
  ASSERT_TRUE(child_events[1].pointer_sample().has_scroll_h());
  EXPECT_EQ(child_events[1].pointer_sample().scroll_h(), -1);
  ASSERT_FALSE(child_events[1].pointer_sample().has_scroll_v_physical_pixel());
  ASSERT_TRUE(child_events[1].pointer_sample().has_scroll_h_physical_pixel());
  EXPECT_EQ(child_events[1].pointer_sample().scroll_h_physical_pixel(), -120.0);
  ASSERT_TRUE(child_events[1].pointer_sample().has_is_precision_scroll());
  EXPECT_TRUE(child_events[1].pointer_sample().is_precision_scroll());

  ASSERT_TRUE(child_events[2].has_pointer_sample());
  ASSERT_TRUE(child_events[2].pointer_sample().has_scroll_v());
  EXPECT_EQ(child_events[2].pointer_sample().scroll_v(), 1);
  ASSERT_TRUE(child_events[2].pointer_sample().has_scroll_h());
  EXPECT_EQ(child_events[2].pointer_sample().scroll_h(), -1);
  ASSERT_TRUE(child_events[2].pointer_sample().has_scroll_v_physical_pixel());
  EXPECT_EQ(child_events[2].pointer_sample().scroll_v_physical_pixel(), 120.0);
  ASSERT_TRUE(child_events[2].pointer_sample().has_scroll_h_physical_pixel());
  EXPECT_EQ(child_events[2].pointer_sample().scroll_h_physical_pixel(), -120.0);
  ASSERT_TRUE(child_events[2].pointer_sample().has_is_precision_scroll());
  EXPECT_TRUE(child_events[2].pointer_sample().is_precision_scroll());

  child_events.clear();

  // without tick, with v physical pixel, is precision scroll
  Inject(0, 0, fupi_EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::nullopt,
         /* scroll_h= */ std::nullopt,
         /* scroll_v_physical_pixel= */ std::optional<double>(120.0),
         /* scroll_h_physical_pixel= */ std::nullopt,

         /* is_precision_scroll= */ std::optional<bool>(true));

  // without tick, with h physical pixel, is precision scroll
  Inject(0, 0, fupi_EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::nullopt,
         /* scroll_h= */ std::nullopt,
         /* scroll_v_physical_pixel= */ std::nullopt,
         /* scroll_h_physical_pixel= */ std::optional<double>(-120.0),
         /* is_precision_scroll= */ std::optional<bool>(true));

  // without tick, with v,h physical pixel, is precision scroll
  Inject(0, 0, fupi_EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::nullopt,
         /* scroll_h= */ std::nullopt,
         /* scroll_v_physical_pixel= */ std::optional<double>(120.0),
         /* scroll_h_physical_pixel= */ std::optional<double>(-120.0),
         /* is_precision_scroll= */ std::optional<bool>(true));

  RunLoopUntil([&child_events] { return child_events.size() == 3u; });

  ASSERT_TRUE(child_events[0].has_pointer_sample());
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_v());
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_h());
  ASSERT_TRUE(child_events[0].pointer_sample().has_scroll_v_physical_pixel());
  EXPECT_EQ(child_events[0].pointer_sample().scroll_v_physical_pixel(), 120.0);
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_h_physical_pixel());
  ASSERT_TRUE(child_events[0].pointer_sample().has_is_precision_scroll());
  EXPECT_TRUE(child_events[0].pointer_sample().is_precision_scroll());

  ASSERT_TRUE(child_events[1].has_pointer_sample());
  ASSERT_FALSE(child_events[1].pointer_sample().has_scroll_v());
  ASSERT_FALSE(child_events[1].pointer_sample().has_scroll_h());
  ASSERT_FALSE(child_events[1].pointer_sample().has_scroll_v_physical_pixel());
  ASSERT_TRUE(child_events[1].pointer_sample().has_scroll_h_physical_pixel());
  EXPECT_EQ(child_events[1].pointer_sample().scroll_h_physical_pixel(), -120.0);
  ASSERT_TRUE(child_events[1].pointer_sample().has_is_precision_scroll());
  EXPECT_TRUE(child_events[1].pointer_sample().is_precision_scroll());

  ASSERT_TRUE(child_events[2].has_pointer_sample());
  ASSERT_FALSE(child_events[2].pointer_sample().has_scroll_v());
  ASSERT_FALSE(child_events[2].pointer_sample().has_scroll_h());
  ASSERT_TRUE(child_events[2].pointer_sample().has_scroll_v_physical_pixel());
  EXPECT_EQ(child_events[2].pointer_sample().scroll_v_physical_pixel(), 120.0);
  ASSERT_TRUE(child_events[2].pointer_sample().has_scroll_h_physical_pixel());
  EXPECT_EQ(child_events[2].pointer_sample().scroll_h_physical_pixel(), -120.0);
  ASSERT_TRUE(child_events[2].pointer_sample().has_is_precision_scroll());
  EXPECT_TRUE(child_events[2].pointer_sample().is_precision_scroll());
}

// Hit tests follow the same basic view topology:
//
// root_session     - context view
//     |
//     |
// parent_session   - target view
//     |
//     |
// child_session
//
// Only the parent and child sessions are eligible to receive hits. This is based on whether they
// have a hit region for a given (x,y), and on the local transform topology of |parent_session|.
// Simply put, the precedence for hits goes towards the transforms added *last* in the
// parent_session's local topology.

// Add full screen hit regions on both parent and child sessions. Check that only the child receives
// hits.
TEST_F(FlatlandMouseIntegrationTest, SimpleHitTest) {
  fuc_FlatlandPtr parent_session;
  fup_MouseSourcePtr parent_mouse_source;
  fuv_ViewRefFocusedPtr parent_focused_ptr;

  parent_session.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  parent_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  parent_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto parent_view_ref = CreateChildView(parent_session, parent_mouse_source.NewRequest(),
                                         parent_focused_ptr.NewRequest());

  fuc_FlatlandPtr child_session;
  fup_MouseSourcePtr child_mouse_source;
  fuv_ViewRefFocusedPtr child_focused_ptr;

  child_session.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  child_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  child_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateAndAddChildView(
      parent_session, /*parent_transform=*/{.value = 2}, kDefaultRootTransform,
      /*parent_content=*/{.value = 2}, child_session, child_mouse_source.NewRequest(),
      child_focused_ptr.NewRequest());

  // Place hit regions, overriding any default ones if they exist.
  parent_session->SetHitRegions(kDefaultRootTransform, {{.region = {0, 0, 10, 10}}});
  child_session->SetHitRegions(kDefaultRootTransform, {{.region = {0, 0, 10, 10}}});

  BlockingPresent(child_session);
  BlockingPresent(parent_session);

  // Listen for input events.
  std::vector<fup_MouseEvent> parent_events;
  StartWatchLoop(parent_mouse_source, parent_events);

  std::vector<fup_MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Inject an input event at (0,0) which is the point of overlap between the parent and the
  // child. The child should receive it.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(parent_view_ref),
                   fupi_DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec,
                   kIdentityMatrix);
  Inject(0, 0, fupi_EventPhase::ADD, button_vec);

  RunLoopUntil([&child_events] { return child_events.size() == 1u; });
  ASSERT_TRUE(child_events[0].has_pointer_sample());
  EXPECT_FALSE(child_events[0].pointer_sample().has_scroll_v());
  EXPECT_FALSE(child_events[0].pointer_sample().has_scroll_h());

  // Verify hit position in viewport.
  std::array<float, 2> position = child_events[0].pointer_sample().position_in_viewport();

  EXPECT_EQ(position[0], 0.f);
  EXPECT_EQ(position[1], 0.f);

  // Parent should have received 0 events.
  EXPECT_EQ(parent_events.size(), 0u);
}

// Add full screen hit regions for both parent and child sessions. This time, the parent adds an
// additional partial-screen overlay on top of the child, which should receive hits instead of the
// child for that portion of the screen. This forms a parent-child-parent "sandwich" for that
// region.
TEST_F(FlatlandMouseIntegrationTest, SandwichTest) {
  fuc_FlatlandPtr parent_session;
  fup_MouseSourcePtr parent_mouse_source;
  fuv_ViewRefFocusedPtr parent_focused_ptr;

  parent_session.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  parent_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  parent_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto parent_view_ref = CreateChildView(parent_session, parent_mouse_source.NewRequest(),
                                         parent_focused_ptr.NewRequest());

  fuc_FlatlandPtr child_session;
  fup_MouseSourcePtr child_mouse_source;
  fuv_ViewRefFocusedPtr child_focused_ptr;

  child_session.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  child_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  child_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateAndAddChildView(
      parent_session, /*parent_transform=*/{.value = 2}, kDefaultRootTransform,
      /*parent_content=*/{.value = 2}, child_session, child_mouse_source.NewRequest(),
      child_focused_ptr.NewRequest());

  // After creating the child transform, create an additional transform representing the overlay.
  fuc_TransformId overlay_transform = {.value = 3};
  parent_session->CreateTransform(overlay_transform);
  parent_session->AddChild(kDefaultRootTransform, overlay_transform);

  // Place hit regions, overriding any default ones if they exist.
  parent_session->SetHitRegions(kDefaultRootTransform, {{.region = {0, 0, 10, 10}}});
  parent_session->SetHitRegions(overlay_transform, {{.region = {0, 0, 5, 5}}});
  child_session->SetHitRegions(kDefaultRootTransform, {{.region = {0, 0, 10, 10}}});

  BlockingPresent(child_session);
  BlockingPresent(parent_session);

  // Listen for input events.
  std::vector<fup_MouseEvent> parent_events;
  StartWatchLoop(parent_mouse_source, parent_events);

  std::vector<fup_MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Inject an input event at (0,0) which is in the sandwich zone. The parent should receive it.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(parent_view_ref),
                   fupi_DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec,
                   kIdentityMatrix);
  Inject(0, 0, fupi_EventPhase::ADD, button_vec);
  RunLoopUntil([&parent_events] { return parent_events.size() == 1u; });
  ASSERT_TRUE(parent_events[0].has_pointer_sample());
  EXPECT_FALSE(parent_events[0].pointer_sample().has_scroll_v());
  EXPECT_FALSE(parent_events[0].pointer_sample().has_scroll_h());

  // Verify hit position in viewport.
  {
    std::array<float, 2> position = parent_events[0].pointer_sample().position_in_viewport();

    EXPECT_EQ(position[0], 0.f);
    EXPECT_EQ(position[1], 0.f);
  }

  // Remove the previous stream.
  Inject(0, 0, fupi_EventPhase::REMOVE, {});
  RunLoopUntil([&parent_events] { return parent_events.size() == 2u; });
  EXPECT_EQ(child_events.size(), 0u);

  // Inject outside of the sandwich zone. The child should receive it.
  Inject(6, 3, fupi_EventPhase::ADD, button_vec);

  RunLoopUntil([&child_events] { return child_events.size() == 1u; });
  ASSERT_TRUE(child_events[0].has_pointer_sample());
  EXPECT_FALSE(child_events[0].pointer_sample().has_scroll_v());
  EXPECT_FALSE(child_events[0].pointer_sample().has_scroll_h());

  // Verify hit position in viewport.
  {
    std::array<float, 2> position = child_events[0].pointer_sample().position_in_viewport();

    EXPECT_EQ(position[0], 6.f);
    EXPECT_EQ(position[1], 3.f);
  }

  // Parent should have received 0 additional events.
  EXPECT_EQ(parent_events.size(), 2u);
}

// In order to test that partial screen views work - this test establishes a context view that is
// translated away from the root view.
//
// ------------------
// |(Root)          |
// |                |
// |                |
// |                |
// |        --------|
// |        |(C/T)  |
// |        |       |
// |        |       |
// ------------------
//
// Root view: 10x10 with origin at (0,0)
// Context and target views: 5x5 with origin at (5,5)
//
//
// root parent context target
TEST_F(FlatlandMouseIntegrationTest, PartialScreenViews) {
  fuc_FlatlandPtr parent_session;
  fup_MouseSourcePtr parent_mouse_source;
  fuv_ViewRefFocusedPtr parent_focused_ptr;

  parent_session.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  parent_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  parent_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto parent_view_ref = CreateChildView(parent_session, parent_mouse_source.NewRequest(),
                                         parent_focused_ptr.NewRequest());

  fuc_FlatlandPtr context_session;
  fup_MouseSourcePtr context_mouse_source;
  fuv_ViewRefFocusedPtr context_focused_ptr;

  context_session.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  context_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  context_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  const fuc_TransformId viewport_transform = {.value = 2};
  auto context_view_ref =
      CreateAndAddChildView(parent_session, viewport_transform, kDefaultRootTransform,
                            /*parent_content=*/{.value = 2}, context_session,
                            context_mouse_source.NewRequest(), context_focused_ptr.NewRequest());

  fuc_FlatlandPtr target_session;
  fup_MouseSourcePtr target_mouse_source;
  fuv_ViewRefFocusedPtr target_focused_ptr;

  target_session.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  target_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  target_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto target_view_ref =
      CreateAndAddChildView(context_session, viewport_transform, kDefaultRootTransform,
                            /*parent_content=*/{.value = 2}, target_session,
                            target_mouse_source.NewRequest(), target_focused_ptr.NewRequest());

  // Change the context view's origin from (0,0) to (5,5).
  int x_translation = 5;
  int y_translation = 5;
  parent_session->SetTranslation(viewport_transform, {x_translation, y_translation});
  fuchsia::math::Rect rect = {0, 0, 5, 5};
  parent_session->SetClipBoundary(viewport_transform,
                                  std::make_unique<fuchsia::math::Rect>(std::move(rect)));

  // Place hit regions, overriding any default ones if they exist.
  parent_session->SetHitRegions(kDefaultRootTransform, {{.region = {0, 0, 10, 10}}});
  context_session->SetHitRegions(kDefaultRootTransform, {{.region = {0, 0, 10, 10}}});
  target_session->SetHitRegions(kDefaultRootTransform, {{.region = {0, 0, 10, 10}}});

  BlockingPresent(parent_session);
  BlockingPresent(context_session);
  BlockingPresent(target_session);

  // Listen for input events.
  std::vector<fup_MouseEvent> context_events;
  StartWatchLoop(context_mouse_source, context_events);

  std::vector<fup_MouseEvent> target_events;
  StartWatchLoop(target_mouse_source, target_events);

  const std::vector<uint8_t> button_vec = {1};

  // Creates this matrix which depicts a 5x5 translation from the input viewport to the context
  // view:
  // 1 0 -5
  // 0 1 -5
  // 0 0 1
  std::array<float, 9> viewport_to_context_transform = {1, 0, 0, 0, 1, 0, -5, -5, 1};

  RegisterInjector(fidl::Clone(context_view_ref), fidl::Clone(target_view_ref),
                   fupi_DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec,
                   viewport_to_context_transform);

  float x = 7;
  float y = 9;

  Inject(x, y, fupi_EventPhase::ADD, button_vec);
  RunLoopUntil([&target_events] { return target_events.size() == 1u; });
  ASSERT_TRUE(target_events[0].has_pointer_sample());

  // Verify hit position in viewport.
  {
    std::array<float, 2> position = target_events[0].pointer_sample().position_in_viewport();

    EXPECT_EQ(position[0], x);
    EXPECT_EQ(position[1], y);
  }

  // Parent should have received 0 events.
  EXPECT_EQ(context_events.size(), 0u);
}

// Set up the following view hierarchy:
//    root    - context view
//     |
//   parent   - target view
//     |
//   child (anonymous)
//     |
//  granchild
//
// All views have fullscreen hit regions, and each subsequent view covers its parent.
// Observe that the anonymous view and its child do not get events or show up in hit tests (and
// block other views from getting events.)
TEST_F(FlatlandMouseIntegrationTest, AnonymousSubtree) {
  fuc_FlatlandPtr parent_session;
  fup_MouseSourcePtr parent_mouse_source;

  parent_session.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  parent_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  const auto parent_view_ref = CreateChildView(parent_session, parent_mouse_source.NewRequest());

  fuc_FlatlandPtr child_session = realm_->Connect<fuc_Flatland>();
  child_session.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });

  {
    // Set up the anonymous child view.
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    fidl::InterfacePtr<fuc_ParentViewportWatcher> parent_viewport_watcher;
    child_session->CreateView(std::move(child_token), parent_viewport_watcher.NewRequest());
    child_session->CreateTransform(kDefaultRootTransform);
    child_session->SetRootTransform(kDefaultRootTransform);
    BlockingPresent(child_session);

    // Attach it to the parent.
    const fuc_TransformId viewport_transform{.value = 2};
    const fuc_ContentId parent_content{.value = 1};
    fidl::InterfacePtr<fuc_ChildViewWatcher> child_view_watcher;
    fuc_ViewportProperties properties;
    properties.set_logical_size({kDefaultSize, kDefaultSize});
    parent_session->CreateTransform(viewport_transform);
    parent_session->CreateViewport(parent_content, std::move(parent_token), std::move(properties),
                                   child_view_watcher.NewRequest());
    parent_session->SetContent(viewport_transform, parent_content);
    parent_session->AddChild(kDefaultRootTransform, viewport_transform);
    BlockingPresent(parent_session);
  }

  // Create the named grandchild view along with its mouse source and attach it to the child.
  fuc_FlatlandPtr grandchild_session;
  fup_MouseSourcePtr grandchild_mouse_source;
  grandchild_session.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  grandchild_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  CreateAndAddChildView(child_session, /*parent_transform=*/{.value = 2}, kDefaultRootTransform,
                        /*parent_content=*/{.value = 2}, grandchild_session,
                        grandchild_mouse_source.NewRequest());

  // Listen for mouse events.
  std::vector<fup_MouseEvent> parent_events;
  StartWatchLoop(parent_mouse_source, parent_events);
  std::vector<fup_MouseEvent> grandchild_events;
  StartWatchLoop(grandchild_mouse_source, grandchild_events);

  // Inject an input event at (0,0) which should hit every view. The anonymous child tree should be
  // ignored and the parent should receive it.
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(parent_view_ref),
                   fupi_DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, {}, kIdentityMatrix);
  Inject(0, 0, fupi_EventPhase::ADD);
  RunLoopUntil([&parent_events] { return parent_events.size() == 1u; });
  EXPECT_TRUE(parent_events[0].has_pointer_sample());
  EXPECT_TRUE(grandchild_events.empty());
}

}  // namespace integration_tests
