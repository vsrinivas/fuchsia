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
#include "src/ui/scenic/tests/utils/utils.h"

// These tests exercise the integration between Flatland and the InputSystem, including the
// View-to-View transform logic between the injection point and the receiver.
// Setup:
// - The test fixture sets up the display + the root instance and view.
// - Injection done in context View Space, with fuchsia.ui.pointerinjector
// - Target(s) specified by View (using view ref koids)
// - Dispatch done to fuchsia.ui.pointer.MouseSource in receiver View Space.
namespace integration_tests {

using ParentViewportWatcher = fuchsia::ui::composition::ParentViewportWatcher;
using ChildViewWatcher = fuchsia::ui::composition::ChildViewWatcher;
using Flatland = fuchsia::ui::composition::Flatland;
using FlatlandPtr = fuchsia::ui::composition::FlatlandPtr;
using ViewBoundProtocols = fuchsia::ui::composition::ViewBoundProtocols;
using FlatlandDisplay = fuchsia::ui::composition::FlatlandDisplay;
using FlatlandDisplayPtr = fuchsia::ui::composition::FlatlandDisplayPtr;
using Orientation = fuchsia::ui::composition::Orientation;
using ViewportProperties = fuchsia::ui::composition::ViewportProperties;
using TransformId = fuchsia::ui::composition::TransformId;
using ContentId = fuchsia::ui::composition::ContentId;
using ViewRefFocused = fuchsia::ui::views::ViewRefFocused;
using ViewRefFocusedPtr = fuchsia::ui::views::ViewRefFocusedPtr;
using ViewRef = fuchsia::ui::views::ViewRef;
using FocusState = fuchsia::ui::views::FocusState;
using MouseEvent = fuchsia::ui::pointer::MouseEvent;
using MouseSource = fuchsia::ui::pointer::MouseSource;
using MouseSourcePtr = fuchsia::ui::pointer::MouseSourcePtr;
using MouseViewStatus = fuchsia::ui::pointer::MouseViewStatus;
using Config = fuchsia::ui::pointerinjector::Config;
using DispatchPolicy = fuchsia::ui::pointerinjector::DispatchPolicy;
using Event = fuchsia::ui::pointerinjector::Event;
using EventPhase = fuchsia::ui::pointerinjector::EventPhase;
using PointerSample = fuchsia::ui::pointerinjector::PointerSample;
using Context = fuchsia::ui::pointerinjector::Context;
using Data = fuchsia::ui::pointerinjector::Data;
using Registry = fuchsia::ui::pointerinjector::Registry;
using RegistryPtr = fuchsia::ui::pointerinjector::RegistryPtr;
using DevicePtr = fuchsia::ui::pointerinjector::DevicePtr;
using DeviceType = fuchsia::ui::pointerinjector::DeviceType;
using Target = fuchsia::ui::pointerinjector::Target;
using Viewport = fuchsia::ui::pointerinjector::Viewport;
using RealmRoot = component_testing::RealmRoot;
using fir_Axis = fuchsia::input::report::Axis;

// Macros for calling EXPECT on fuchsia::ui::pointer::MousePointerSample.
// Delegates to ExpectEqualPointer(), but are macros to ensure we get the correct line number for
// the error.
#define EXPECT_EQ_POINTER_WITH_SCROLL_AND_BUTTONS(pointer_sample, viewport_to_view_transform, \
                                                  expected_x, expected_y, expected_scroll_v,  \
                                                  expected_scroll_h, expected_buttons)        \
  ExpectEqualPointer(pointer_sample, viewport_to_view_transform, expected_x, expected_y,      \
                     expected_scroll_v, expected_scroll_h, expected_buttons, __LINE__);

#define EXPECT_EQ_POINTER_WITH_SCROLL(pointer_sample, viewport_to_view_transform, expected_x, \
                                      expected_y, expected_scroll_v, expected_scroll_h)       \
  EXPECT_EQ_POINTER_WITH_SCROLL_AND_BUTTONS(pointer_sample, viewport_to_view_transform,       \
                                            expected_x, expected_y, expected_scroll_v,        \
                                            expected_scroll_h, std::vector<uint8_t>());

#define EXPECT_EQ_POINTER_WITH_BUTTONS(pointer_sample, viewport_to_view_transform, expected_x, \
                                       expected_y, expected_buttons)                           \
  EXPECT_EQ_POINTER_WITH_SCROLL_AND_BUTTONS(pointer_sample, viewport_to_view_transform,        \
                                            expected_x, expected_y, std::optional<int64_t>(),  \
                                            std::optional<int64_t>(), expected_buttons);

#define EXPECT_EQ_POINTER(pointer_sample, viewport_to_view_transform, expected_x, expected_y) \
  EXPECT_EQ_POINTER_WITH_BUTTONS(pointer_sample, viewport_to_view_transform, expected_x,      \
                                 expected_y, std::vector<uint8_t>());

std::array<float, 2> TransformPointerCoords(std::array<float, 2> pointer, const Mat3& transform) {
  const Vec3 homogenous_pointer = {pointer[0], pointer[1], 1};
  Vec3 transformed_pointer = transform * homogenous_pointer;
  FX_CHECK(transformed_pointer[2] != 0);
  const Vec3& homogenized = transformed_pointer / transformed_pointer[2];
  return {homogenized[0], homogenized[1]};
}

void ExpectEqualPointer(const fuchsia::ui::pointer::MousePointerSample& pointer_sample,
                        const std::array<float, 9>& viewport_to_view_transform, float expected_x,
                        float expected_y, std::optional<int64_t> expected_scroll_v,
                        std::optional<int64_t> expected_scroll_h,
                        std::vector<uint8_t> expected_buttons, uint32_t line_number) {
  const Mat3 transform_matrix = ArrayToMat3(viewport_to_view_transform);
  const std::array<float, 2> transformed_pointer =
      TransformPointerCoords(pointer_sample.position_in_viewport(), transform_matrix);
  EXPECT_TRUE(CmpFloatingValues(transformed_pointer[0], expected_x), "Line: %d", line_number);
  EXPECT_TRUE(CmpFloatingValues(transformed_pointer[1], expected_y), "Line: %d", line_number);
  if (expected_scroll_v.has_value()) {
    ASSERT_TRUE(pointer_sample.has_scroll_v(), "Line: %d", line_number);
    EXPECT_EQ(pointer_sample.scroll_v(), expected_scroll_v.value(), "Line: %d", line_number);
  } else {
    EXPECT_FALSE(pointer_sample.has_scroll_v(), "Line: %d", line_number);
  }
  if (expected_scroll_h.has_value()) {
    ASSERT_TRUE(pointer_sample.has_scroll_h(), "Line: %d", line_number);
    EXPECT_EQ(pointer_sample.scroll_h(), expected_scroll_h.value(), "Line: %d", line_number);
  } else {
    EXPECT_FALSE(pointer_sample.has_scroll_h(), "Line: %d", line_number);
  }
  if (expected_buttons.empty()) {
    EXPECT_FALSE(pointer_sample.has_pressed_buttons(), "Line: %d", line_number);
  } else {
    ASSERT_TRUE(pointer_sample.has_pressed_buttons(), "Line: %d", line_number);
    ASSERT_EQ(pointer_sample.pressed_buttons().size(), expected_buttons.size());
    for (uint8_t i = 0; i < pointer_sample.pressed_buttons().size(); i++) {
      EXPECT_EQ(pointer_sample.pressed_buttons()[i], expected_buttons[i], "Line: %d", line_number);
    }
  }
}

class FlatlandMouseIntegrationTest : public zxtest::Test, public loop_fixture::RealLoop {
 protected:
  static constexpr uint32_t kDeviceId = 1111;

  static constexpr uint32_t kPointerId = 2222;

  static constexpr uint32_t kDefaultSize = 10;

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

    flatland_display_ = realm_->Connect<FlatlandDisplay>();
    flatland_display_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
    });

    pointerinjector_registry_ = realm_->Connect<Registry>();
    pointerinjector_registry_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to pointerinjector Registry: %s", zx_status_get_string(status));
    });

    // Set up root view.
    root_instance_ = realm_->Connect<Flatland>();
    root_instance_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
    });

    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    ViewBoundProtocols protocols;
    ViewRefFocusedPtr root_focused_ptr;

    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    root_view_ref_ = fidl::Clone(identity.view_ref);
    protocols.set_view_ref_focused(root_focused_ptr.NewRequest());

    root_instance_->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                                parent_viewport_watcher.NewRequest());

    parent_viewport_watcher->GetLayout([this](auto layout_info) {
      ASSERT_TRUE(layout_info.has_logical_size());
      const auto [width, height] = layout_info.logical_size();
      display_width_ = static_cast<float>(width);
      display_height_ = static_cast<float>(height);
    });

    flatland_display_->SetContent(std::move(parent_token), child_view_watcher.NewRequest());
    BlockingPresent(root_instance_);

    // Wait until we get the display size.
    RunLoopUntil([this] { return display_width_ != 0 && display_height_ != 0; });
  }

  void BlockingPresent(FlatlandPtr& flatland) {
    bool presented = false;
    flatland.events().OnFramePresented = [&presented](auto) { presented = true; };
    flatland->Present({});
    RunLoopUntil([&presented] { return presented; });
    flatland.events().OnFramePresented = nullptr;
  }

  void Inject(float x, float y, EventPhase phase, std::vector<uint8_t> pressed_buttons = {},
              std::optional<int64_t> scroll_v = std::nullopt,
              std::optional<int64_t> scroll_h = std::nullopt,
              std::optional<double> scroll_v_physical_pixel = std::nullopt,
              std::optional<double> scroll_h_physical_pixel = std::nullopt,
              std::optional<bool> is_precision_scroll = std::nullopt) {
    FX_DCHECK(injector_);
    Event event;
    event.set_timestamp(0);
    {
      PointerSample pointer_sample;
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
      Data data;
      data.set_pointer_sample(std::move(pointer_sample));
      event.set_data(std::move(data));
    }
    std::vector<Event> events;
    events.emplace_back(std::move(event));
    injector_->Inject(std::move(events), [] {});
  }

  void InjectNewViewport(Viewport viewport) {
    fuchsia::ui::pointerinjector::Event event;
    event.set_timestamp(0);
    {
      fuchsia::ui::pointerinjector::Data data;
      data.set_viewport(std::move(viewport));
      event.set_data(std::move(data));
    }
    std::vector<fuchsia::ui::pointerinjector::Event> events;
    events.emplace_back(std::move(event));
    bool hanging_get_returned = false;
    injector_->Inject(std::move(events), [&hanging_get_returned] { hanging_get_returned = true; });
    RunLoopUntil([&hanging_get_returned] { return hanging_get_returned; });
  }

  void RegisterInjector(ViewRef context_view_ref, ViewRef target_view_ref,
                        DispatchPolicy dispatch_policy, std::vector<uint8_t> buttons,
                        std::array<float, 9> viewport_to_context_transform) {
    Config config;
    config.set_device_id(kDeviceId);
    config.set_device_type(DeviceType::MOUSE);
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
        Context context;
        context.set_view(std::move(context_view_ref));
        config.set_context(std::move(context));
      }
      {
        Target target;
        target.set_view(std::move(target_view_ref));
        config.set_target(std::move(target));
      }
      {
        Viewport viewport;
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
  void StartWatchLoop(MouseSourcePtr& mouse_source, std::vector<MouseEvent>& out_events) {
    const size_t index = watch_loops_.size();
    watch_loops_.emplace_back();
    watch_loops_.at(index) = [this, &mouse_source, &out_events,
                              index](std::vector<MouseEvent> events) {
      std::move(events.begin(), events.end(), std::back_inserter(out_events));
      mouse_source->Watch([this, index](std::vector<MouseEvent> events) {
        watch_loops_.at(index)(std::move(events));
      });
    };
    mouse_source->Watch(watch_loops_.at(index));
  }

  std::array<std::array<float, 2>, 2> FullScreenExtents() const {
    return {{{0, 0}, {display_width_, display_height_}}};
  }

  ViewRef CreateChildView(FlatlandPtr& child_instance,
                          fidl::InterfaceRequest<MouseSource> child_mouse_source = nullptr,
                          fidl::InterfaceRequest<ViewRefFocused> child_focused_ptr = nullptr) {
    root_instance_->CreateTransform(kRootTransform);
    root_instance_->SetRootTransform(kRootTransform);
    return CreateAndAddChildView(root_instance_,
                                 /*viewport_transform*/ {.value = kRootTransform.value + 1},
                                 /*parent_of_viewport_transform*/ kRootTransform,
                                 /*parent_content*/ {.value = 1}, child_instance,
                                 std::move(child_mouse_source), std::move(child_focused_ptr));
  }

  // This function assumes the parent_instance was created via |CreateChildView()|. This assumption
  // means that the transform topology is a root transform with one level of N children. This
  // enables virtually every hit testing scenario with minimal test complexity.
  //
  // Prereq: |parent_of_viewport_transform| is created and connected to the view's root.
  ViewRef CreateAndAddChildView(
      FlatlandPtr& parent_instance, TransformId viewport_transform,
      TransformId parent_of_viewport_transform, ContentId parent_content,
      FlatlandPtr& child_instance, fidl::InterfaceRequest<MouseSource> child_mouse_source = nullptr,
      fidl::InterfaceRequest<ViewRefFocused> child_focused_ptr = nullptr) {
    child_instance = realm_->Connect<Flatland>();

    // Set up the child view watcher.
    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    ViewportProperties properties;
    properties.set_logical_size({kDefaultSize, kDefaultSize});

    parent_instance->CreateTransform(viewport_transform);
    parent_instance->CreateViewport(parent_content, std::move(parent_token), std::move(properties),
                                    child_view_watcher.NewRequest());
    parent_instance->SetContent(viewport_transform, parent_content);
    parent_instance->AddChild(parent_of_viewport_transform, viewport_transform);

    BlockingPresent(parent_instance);

    // Set up the child view along with its MouseSource and ViewRefFocused channel.
    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    auto child_view_ref = fidl::Clone(identity.view_ref);
    ViewBoundProtocols protocols;
    if (child_mouse_source)
      protocols.set_mouse_source(std::move(child_mouse_source));
    if (child_focused_ptr)
      protocols.set_view_ref_focused(std::move(child_focused_ptr));
    child_instance->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                                parent_viewport_watcher.NewRequest());
    child_instance->CreateTransform(kRootTransform);
    child_instance->SetRootTransform(kRootTransform);
    BlockingPresent(child_instance);

    return child_view_ref;
  }

  static constexpr TransformId kRootTransform{.value = 1};
  static constexpr ContentId kRootContentId{.value = 1};

  fuchsia::math::SizeU FullscreenSize() {
    return {static_cast<uint32_t>(display_width_), static_cast<uint32_t>(display_height_)};
  }

  FlatlandPtr root_instance_;

  ViewRef root_view_ref_;

  bool injector_channel_closed_ = false;

  float display_width_ = 0;

  float display_height_ = 0;

  std::unique_ptr<RealmRoot> realm_;

 private:
  FlatlandDisplayPtr flatland_display_;

  RegistryPtr pointerinjector_registry_;

  DevicePtr injector_;

  // Holds watch loops so they stay alive through the duration of the test.
  std::vector<std::function<void(std::vector<MouseEvent>)>> watch_loops_;
};

TEST_F(FlatlandMouseIntegrationTest, ReleaseTargetView_TriggersChannelClosure) {
  FlatlandPtr child_instance;
  MouseSourcePtr child_mouse_source;
  ViewRefFocusedPtr child_focused_ptr;

  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_instance, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec, kIdentityMatrix);

  // Break the scene graph relation that the pointerinjector relies on. Observe the channel close
  // (lazily).
  child_instance->ReleaseView();
  BlockingPresent(child_instance);

  // Inject an event to trigger the channel closure.
  Inject(0, 0, EventPhase::ADD, button_vec);
  RunLoopUntil([this] { return injector_channel_closed_; });  // Succeeds or times out.
}

TEST_F(FlatlandMouseIntegrationTest, DisconnectTargetView_TriggersChannelClosure) {
  FlatlandPtr child_instance;
  MouseSourcePtr child_mouse_source;
  ViewRefFocusedPtr child_focused_ptr;

  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_instance, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec, kIdentityMatrix);

  // Break the scene graph relation that the pointerinjector relies on. Observe the channel close
  // (lazily).
  root_instance_->RemoveChild(kRootTransform, {.value = kRootTransform.value + 1});
  BlockingPresent(root_instance_);

  // Inject an event to trigger the channel closure.
  Inject(0, 0, EventPhase::ADD, button_vec);
  RunLoopUntil([this] { return injector_channel_closed_; });  // Succeeds or times out.
}

// The child view should receive focus and input events when the mouse button is pressed over its
// view.
TEST_F(FlatlandMouseIntegrationTest, ChildReceivesFocus_OnMouseLatch) {
  FlatlandPtr child_instance;
  MouseSourcePtr child_mouse_source;
  ViewRefFocusedPtr child_focused_ptr;

  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  child_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  child_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_instance, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Inject an input event at (0,0) which is the point of overlap between the parent and the
  // child.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec, kIdentityMatrix);
  Inject(0, 0, EventPhase::ADD, button_vec);

  // Child should receive mouse input events.
  RunLoopUntil([&child_events] { return child_events.size() == 1u; });

  // Child view should receive focus.
  std::optional<FocusState> child_focused;
  child_focused_ptr->Watch([&child_focused](auto update) { child_focused = std::move(update); });
  RunLoopUntil([&child_focused] { return child_focused.has_value(); });
  EXPECT_TRUE(child_focused->focused());
}

// Send wheel events to scenic ensure client receives wheel events.
TEST_F(FlatlandMouseIntegrationTest, Wheel) {
  FlatlandPtr child_instance;
  MouseSourcePtr child_mouse_source;
  ViewRefFocusedPtr child_focused_ptr;

  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  child_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  child_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_instance, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Inject an input event at (0,0) which is the point of overlap between the parent and the
  // child.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec, kIdentityMatrix);
  Inject(0, 0, EventPhase::ADD, button_vec);
  Inject(0, 0, EventPhase::CHANGE, /* pressed_buttons= */ {},
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
  FlatlandPtr child_instance;
  MouseSourcePtr child_mouse_source;
  ViewRefFocusedPtr child_focused_ptr;

  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  child_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  child_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_instance, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Inject an input event at (0,0) which is the point of overlap between the parent and the
  // child.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec, kIdentityMatrix);
  Inject(0, 0, EventPhase::ADD, button_vec);
  Inject(0, 0, EventPhase::CHANGE, button_vec);
  Inject(0, 0, EventPhase::CHANGE, button_vec,
         /* scroll_v= */ std::optional<int64_t>(1));
  Inject(0, 0, EventPhase::CHANGE, /* pressed_buttons= */ {});
  Inject(0, 0, EventPhase::CHANGE, /* pressed_buttons= */ {},
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
  FlatlandPtr child_instance;
  MouseSourcePtr child_mouse_source;
  ViewRefFocusedPtr child_focused_ptr;

  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  child_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  child_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_instance, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Inject an input event at (0,0) which is the point of overlap between the parent and the
  // child.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec, kIdentityMatrix);
  Inject(0, 0, EventPhase::ADD, button_vec);
  // This event bundled button down and wheel.
  Inject(0, 0, EventPhase::CHANGE, button_vec, /* scroll_v= */ std::optional<int64_t>(1));
  Inject(0, 0, EventPhase::CHANGE, button_vec, /* scroll_v= */ std::optional<int64_t>(1));
  // This event bundled button up and wheel.
  Inject(0, 0, EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::optional<int64_t>(1));
  Inject(0, 0, EventPhase::CHANGE, /* pressed_buttons= */ {},
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
  FlatlandPtr child_instance;
  MouseSourcePtr child_mouse_source;
  ViewRefFocusedPtr child_focused_ptr;

  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  child_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  child_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_instance, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Inject an input event at (0,0) which is the point of overlap between the parent and the
  // child.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec, kIdentityMatrix);
  Inject(0, 0, EventPhase::ADD, button_vec);

  RunLoopUntil([&child_events] { return child_events.size() == 1u; });
  ASSERT_TRUE(child_events[0].has_pointer_sample());
  EXPECT_FALSE(child_events[0].pointer_sample().has_scroll_v());
  EXPECT_FALSE(child_events[0].pointer_sample().has_scroll_h());
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_v_physical_pixel());
  ASSERT_FALSE(child_events[0].pointer_sample().has_scroll_h_physical_pixel());
  ASSERT_FALSE(child_events[0].pointer_sample().has_is_precision_scroll());
  child_events.clear();

  // with v physical pixel, not precision scroll
  Inject(0, 0, EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::optional<int64_t>(1),
         /* scroll_h= */ std::nullopt,
         /* scroll_v_physical_pixel= */ std::optional<double>(120.0),
         /* scroll_h_physical_pixel= */ std::nullopt,
         /* is_precision_scroll= */ std::optional<bool>(false));

  // with h physical pixel, not precision scroll
  Inject(0, 0, EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::nullopt,
         /* scroll_h= */ std::optional<int64_t>(-1),
         /* scroll_v_physical_pixel= */ std::nullopt,
         /* scroll_h_physical_pixel= */ std::optional<double>(-120.0),
         /* is_precision_scroll= */ std::optional<bool>(false));

  // with v,h physical pixel, not precision scroll
  Inject(0, 0, EventPhase::CHANGE, /* pressed_buttons= */ {},
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
  Inject(0, 0, EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::optional<int64_t>(1),
         /* scroll_h= */ std::nullopt,
         /* scroll_v_physical_pixel= */ std::optional<double>(120.0),
         /* scroll_h_physical_pixel= */ std::nullopt,
         /* is_precision_scroll= */ std::optional<bool>(true));

  // with h physical pixel, is precision scroll
  Inject(0, 0, EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::nullopt,
         /* scroll_h= */ std::optional<int64_t>(-1),
         /* scroll_v_physical_pixel= */ std::nullopt,
         /* scroll_h_physical_pixel= */ std::optional<double>(-120.0),
         /* is_precision_scroll= */ std::optional<bool>(true));

  // with v,h physical pixel, is precision scroll
  Inject(0, 0, EventPhase::CHANGE, /* pressed_buttons= */ {},
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
  Inject(0, 0, EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::nullopt,
         /* scroll_h= */ std::nullopt,
         /* scroll_v_physical_pixel= */ std::optional<double>(120.0),
         /* scroll_h_physical_pixel= */ std::nullopt,

         /* is_precision_scroll= */ std::optional<bool>(true));

  // without tick, with h physical pixel, is precision scroll
  Inject(0, 0, EventPhase::CHANGE, /* pressed_buttons= */ {},
         /* scroll_v= */ std::nullopt,
         /* scroll_h= */ std::nullopt,
         /* scroll_v_physical_pixel= */ std::nullopt,
         /* scroll_h_physical_pixel= */ std::optional<double>(-120.0),
         /* is_precision_scroll= */ std::optional<bool>(true));

  // without tick, with v,h physical pixel, is precision scroll
  Inject(0, 0, EventPhase::CHANGE, /* pressed_buttons= */ {},
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
// root_instance     - context view
//     |
//     |
// parent_instance   - target view
//     |
//     |
// child_instance
//
// Only the parent and child instances are eligible to receive hits. This is based on whether they
// have a hit region for a given (x,y), and on the local transform topology of |parent_instance|.
// Simply put, the precedence for hits goes towards the transforms added *last* in the
// parent_instance's local topology.

// Add full screen hit regions on both parent and child instances. Check that only the child
// receives hits.
TEST_F(FlatlandMouseIntegrationTest, SimpleHitTest) {
  FlatlandPtr parent_instance;
  MouseSourcePtr parent_mouse_source;
  ViewRefFocusedPtr parent_focused_ptr;

  parent_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  parent_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  parent_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto parent_view_ref = CreateChildView(parent_instance, parent_mouse_source.NewRequest(),
                                         parent_focused_ptr.NewRequest());

  FlatlandPtr child_instance;
  MouseSourcePtr child_mouse_source;
  ViewRefFocusedPtr child_focused_ptr;

  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  child_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  child_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto child_view_ref =
      CreateAndAddChildView(parent_instance, /*parent_transform=*/{.value = 2}, kRootTransform,
                            /*parent_content=*/{.value = 2}, child_instance,
                            child_mouse_source.NewRequest(), child_focused_ptr.NewRequest());

  // Place hit regions, overriding any default ones if they exist.
  parent_instance->SetHitRegions(kRootTransform, {{.region = {0, 0, 10, 10}}});
  child_instance->SetHitRegions(kRootTransform, {{.region = {0, 0, 10, 10}}});

  BlockingPresent(child_instance);
  BlockingPresent(parent_instance);

  // Listen for input events.
  std::vector<MouseEvent> parent_events;
  StartWatchLoop(parent_mouse_source, parent_events);

  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Inject an input event at (0,0) which is the point of overlap between the parent and the
  // child. The child should receive it.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(parent_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec, kIdentityMatrix);
  Inject(0, 0, EventPhase::ADD, button_vec);

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

// Add full screen hit regions for both parent and child instances. This time, the parent adds an
// additional partial-screen overlay on top of the child, which should receive hits instead of the
// child for that portion of the screen. This forms a parent-child-parent "sandwich" for that
// region.
TEST_F(FlatlandMouseIntegrationTest, SandwichTest) {
  FlatlandPtr parent_instance;
  MouseSourcePtr parent_mouse_source;
  ViewRefFocusedPtr parent_focused_ptr;

  parent_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  parent_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  parent_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto parent_view_ref = CreateChildView(parent_instance, parent_mouse_source.NewRequest(),
                                         parent_focused_ptr.NewRequest());

  FlatlandPtr child_instance;
  MouseSourcePtr child_mouse_source;
  ViewRefFocusedPtr child_focused_ptr;

  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  child_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  child_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto child_view_ref =
      CreateAndAddChildView(parent_instance, /*parent_transform=*/{.value = 2}, kRootTransform,
                            /*parent_content=*/{.value = 2}, child_instance,
                            child_mouse_source.NewRequest(), child_focused_ptr.NewRequest());

  // After creating the child transform, create an additional transform representing the overlay.
  TransformId overlay_transform = {.value = 3};
  parent_instance->CreateTransform(overlay_transform);
  parent_instance->AddChild(kRootTransform, overlay_transform);

  // Place hit regions, overriding any default ones if they exist.
  parent_instance->SetHitRegions(kRootTransform, {{.region = {0, 0, 10, 10}}});
  parent_instance->SetHitRegions(overlay_transform, {{.region = {0, 0, 5, 5}}});
  child_instance->SetHitRegions(kRootTransform, {{.region = {0, 0, 10, 10}}});

  BlockingPresent(child_instance);
  BlockingPresent(parent_instance);

  // Listen for input events.
  std::vector<MouseEvent> parent_events;
  StartWatchLoop(parent_mouse_source, parent_events);

  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Inject an input event at (0,0) which is in the sandwich zone. The parent should receive it.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(parent_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec, kIdentityMatrix);
  Inject(0, 0, EventPhase::ADD, button_vec);
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
  Inject(0, 0, EventPhase::REMOVE, {});
  RunLoopUntil([&parent_events] { return parent_events.size() == 2u; });
  EXPECT_EQ(child_events.size(), 0u);

  // Inject outside of the sandwich zone. The child should receive it.
  Inject(6, 3, EventPhase::ADD, button_vec);

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
  FlatlandPtr parent_instance;
  MouseSourcePtr parent_mouse_source;
  ViewRefFocusedPtr parent_focused_ptr;

  parent_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  parent_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  parent_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto parent_view_ref = CreateChildView(parent_instance, parent_mouse_source.NewRequest(),
                                         parent_focused_ptr.NewRequest());

  FlatlandPtr context_instance;
  MouseSourcePtr context_mouse_source;
  ViewRefFocusedPtr context_focused_ptr;

  context_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  context_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  context_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  const TransformId viewport_transform = {.value = 2};
  auto context_view_ref =
      CreateAndAddChildView(parent_instance, viewport_transform, kRootTransform,
                            /*parent_content=*/{.value = 2}, context_instance,
                            context_mouse_source.NewRequest(), context_focused_ptr.NewRequest());

  FlatlandPtr target_instance;
  MouseSourcePtr target_mouse_source;
  ViewRefFocusedPtr target_focused_ptr;

  target_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  target_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  target_focused_ptr.set_error_handler([](zx_status_t status) {
    FAIL("ViewRefFocused closed with status: %s", zx_status_get_string(status));
  });

  auto target_view_ref =
      CreateAndAddChildView(context_instance, viewport_transform, kRootTransform,
                            /*parent_content=*/{.value = 2}, target_instance,
                            target_mouse_source.NewRequest(), target_focused_ptr.NewRequest());

  // Change the context view's origin from (0,0) to (5,5).
  int x_translation = 5;
  int y_translation = 5;
  parent_instance->SetTranslation(viewport_transform, {x_translation, y_translation});
  fuchsia::math::Rect rect = {0, 0, 5, 5};
  parent_instance->SetClipBoundary(viewport_transform,
                                   std::make_unique<fuchsia::math::Rect>(std::move(rect)));

  // Place hit regions, overriding any default ones if they exist.
  parent_instance->SetHitRegions(kRootTransform, {{.region = {0, 0, 10, 10}}});
  context_instance->SetHitRegions(kRootTransform, {{.region = {0, 0, 10, 10}}});
  target_instance->SetHitRegions(kRootTransform, {{.region = {0, 0, 10, 10}}});

  BlockingPresent(parent_instance);
  BlockingPresent(context_instance);
  BlockingPresent(target_instance);

  // Listen for input events.
  std::vector<MouseEvent> context_events;
  StartWatchLoop(context_mouse_source, context_events);

  std::vector<MouseEvent> target_events;
  StartWatchLoop(target_mouse_source, target_events);

  const std::vector<uint8_t> button_vec = {1};

  // Creates this matrix which depicts a 5x5 translation from the input viewport to the context
  // view:
  // 1 0 -5
  // 0 1 -5
  // 0 0 1
  std::array<float, 9> viewport_to_context_transform = {1, 0, 0, 0, 1, 0, -5, -5, 1};

  RegisterInjector(fidl::Clone(context_view_ref), fidl::Clone(target_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec,
                   viewport_to_context_transform);

  float x = 7;
  float y = 9;

  Inject(x, y, EventPhase::ADD, button_vec);
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

// In this test we set up the context and the target. We apply a scale, rotation and translation
// transform to both of their view holder nodes, and then inject pointer events to confirm that
// the coordinates received by the listener are correctly transformed.
// Only the transformation of the target, relative to the context, should have any effect on
// the output.
// The viewport-to-context transform here is the identity. That is, the size of the 9x9 viewport
// matches the size of the 5x5 context view.
//
// Below are ASCII diagrams showing the transformation *difference* between target and context.
// Note that the dashes represent the context view and notated X,Y coordinate system is the
// context's coordinate system. The target view's coordinate system has its origin at corner '1'.
//
// Scene pre-transformation
// 1,2,3,4 denote the corners of the target view:
//   X ->
// Y 1 O O O O 2
// | O O O O O O
// v O O O O O O
//   O O O O O O
//   O O O O O O
//   4 O O O O 3
//
// After scale:
//   X ->
// Y 1 - O - O - O   O   2
// | - - - - - - -
// V - - - - - - -
//   O - O - O - O   O   O
//   - - - - - - -
//   - - - - - - -
//   O   O   O   O   O   O
//
//
//   O   O   O   O   O   O
//
//
//   O   O   O   O   O   O
//
//
//   4   O   O   O   O   3
//
// After rotation:
//   X ->
// Y 4      O      O      O      O      1 - - - - - -
// |                                      - - - - - -
// V O      O      O      O      O      O - - - - - -
//                                        - - - - - -
//   O      O      O      O      O      O - - - - - -
//                                        - - - - - -
//   O      O      O      O      O      O
//
//   O      O      O      O      O      O
//
//   3      O      O      O      O      2
//
// After translation:
//   X ->
// Y 4      O      O      O      O    A 1 - - - C1
// |                                  - - - - - -
// V O      O      O      O      O    - O - - - -
//                                    - - - - - -
//   O      O      O      O      O    - O - - - -
//                                    R - - - - C2
//   O      O      O      O      O      O
//
//   O      O      O      O      O      O
//
//   3      O      O      O      O      2
TEST_F(FlatlandMouseIntegrationTest, TargetViewWith_ScaleRotationTranslation) {
  FlatlandPtr child_instance;
  MouseSourcePtr child_mouse_source;
  ViewRefFocusedPtr child_focused_ptr;

  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_instance, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Scale, rotate, and translate the child_instance. Those operations are applied in that order.
  TransformId kTransformId = {.value = 2};
  root_instance_->SetScale(kTransformId, {2, 3});
  root_instance_->SetOrientation(kTransformId, Orientation::CCW_270_DEGREES);
  root_instance_->SetTranslation(kTransformId, {1, 0});
  BlockingPresent(root_instance_);

  // Listen for input events.
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Scene is now set up, send in the input. One event for each corner of the view.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec, kIdentityMatrix);

  Inject(0, 0, EventPhase::ADD, button_vec);
  Inject(10, 0, EventPhase::CHANGE, button_vec);
  Inject(0, 10, EventPhase::CHANGE, button_vec);
  Inject(10, 10, EventPhase::CHANGE, button_vec);

  RunLoopUntil([&child_events] { return child_events.size() == 4u; });  // Succeeds or times out.

  {  // Check layout validity.
    EXPECT_EQ(child_events[0].device_info().id(), kDeviceId);
    const auto& view_parameters = child_events[0].view_parameters();
    EXPECT_TRUE(CmpFloatingValues(view_parameters.view.min[0], 0.f));
    EXPECT_TRUE(CmpFloatingValues(view_parameters.view.min[1], 0.f));
    EXPECT_TRUE(CmpFloatingValues(view_parameters.view.max[0], 10.f));
    EXPECT_TRUE(CmpFloatingValues(view_parameters.view.max[1], 10.f));
    EXPECT_TRUE(CmpFloatingValues(view_parameters.viewport.min[0], 0.f));
    EXPECT_TRUE(CmpFloatingValues(view_parameters.viewport.min[1], 0.f));
    EXPECT_TRUE(CmpFloatingValues(view_parameters.viewport.max[0], display_width_));
    EXPECT_TRUE(CmpFloatingValues(view_parameters.viewport.max[1], display_height_));
  }

  // For a CCW_270 rotation, the new x' and y' from x and y is:
  // x' = y
  // y' = -x
  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER_WITH_BUTTONS(child_events[0].pointer_sample(), viewport_to_view_transform,
                                   0.f / 2.f, (0.f + 1.f) / 3.f, button_vec);
    EXPECT_EQ_POINTER_WITH_BUTTONS(child_events[1].pointer_sample(), viewport_to_view_transform,
                                   0.f / 2.f, (-10.f + 1.f) / 3.f, button_vec);
    EXPECT_EQ_POINTER_WITH_BUTTONS(child_events[2].pointer_sample(), viewport_to_view_transform,
                                   10.f / 2.f, (0.f + 1.f) / 3.f, button_vec);
    EXPECT_EQ_POINTER_WITH_BUTTONS(child_events[3].pointer_sample(), viewport_to_view_transform,
                                   10.f / 2.f, (-10.f + 1.f) / 3.f, button_vec);
  }
}

// In this test the context and the target have identical coordinate systems, but the viewport
// no longer matches the context's coordinate system.
//
// Below is an ASCII diagram showing the resulting setup.
// O represents the views, - the viewport.
//   X ->
// Y O   O   O   O   O   O
// |
// V   A - - - - C1- - - -
//   O - O - O - O - O - O
//     - - - - - - - - - -
//     - - - - - - - - - -
//   O - O - O - O - O - O
//     R - - - - C2- - - -
//     - - - - - - - - - -
//   O - O - O - O - O - O
//     - - - - - - - - - -
//     - - - - - - - - - -
//   O   O   O   O   O   O
//
//
//   O   O   O   O   O   O
//
TEST_F(FlatlandMouseIntegrationTest, InjectedInput_ShouldBeCorrectlyViewportTransformed) {
  FlatlandPtr child_instance;
  MouseSourcePtr child_mouse_source;
  ViewRefFocusedPtr child_focused_ptr;

  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_instance, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Transform to scale the viewport by 1/2 in the x-direction, 1/3 in the y-direction,
  // and then translate by (1, 2).
  // clang-format off
  static constexpr std::array<float, 9> kViewportToContextTransform = {
    1.f/2.f,        0,  0, // first column
          0,  1.f/3.f,  0, // second column
          1,        2,  1, // third column
  };
  // clang-format on

  // Scene is now set up, send in the input. One event for each corner of the view.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec,
                   kViewportToContextTransform);

  // Scene is now set up, send in the input. One event for where each corner of the view was
  // pre-transformation.

  Inject(0, 0, EventPhase::ADD);                                        // A
  Inject(5, 0, EventPhase::CHANGE);                                     // C1
  Inject(5, 5, EventPhase::CHANGE);                                     // C2
  Inject(0, 5, EventPhase::CHANGE);                                     // R
  RunLoopUntil([&child_events] { return child_events.size() == 4u; });  // Succeeds or times out.

  // Check pointer samples.
  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER(child_events[0].pointer_sample(), viewport_to_view_transform, 0.f / 2.f + 1,
                      0.f / 3.f + 2);
    EXPECT_EQ_POINTER(child_events[1].pointer_sample(), viewport_to_view_transform, 5.f / 2.f + 1,
                      0.f / 3.f + 2);
    EXPECT_EQ_POINTER(child_events[2].pointer_sample(), viewport_to_view_transform, 5.f / 2.f + 1,
                      5.f / 3.f + 2);
    EXPECT_EQ_POINTER(child_events[3].pointer_sample(), viewport_to_view_transform, 0.f / 2.f + 1,
                      5.f / 3.f + 2);
  }
}

// In this test the context and the target have identical coordinate systems except for a 90 degree
// rotation. Check that all corners still generate hits. This confirms that small floating point
// errors don't cause misses.
//
// Scene pre-transformation
// 1,2,3,4 denote the corners of the target view:
//   X ->
// Y 1 O O O O 2
// | O O O O O O
// v O O O O O O
//   O O O O O O
//   O O O O O O
//   4 O O O O 3
//
// Post-rotation
//   X ->
// Y 4 O O O O 1
// | O O O O O O
// v O O O O O O
//   O O O O O O
//   O O O O O O
//   3 O O O O 2
TEST_F(FlatlandMouseIntegrationTest, InjectedInput_OnRotatedChild_ShouldHitEdges) {
  FlatlandPtr child_instance;
  MouseSourcePtr child_mouse_source;
  ViewRefFocusedPtr child_focused_ptr;

  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_instance, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Apply rotation.
  TransformId transform = {.value = kRootTransform.value + 1};
  root_instance_->SetOrientation(transform, Orientation::CCW_270_DEGREES);
  root_instance_->SetTranslation(transform, {5, 0});
  BlockingPresent(root_instance_);

  // Listen for input events.
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Scene is now set up, send in the input. One interaction for each corner.
  const std::vector<uint8_t> button_vec = {1};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec, kIdentityMatrix);

  Inject(0, 0, EventPhase::ADD);
  Inject(0, 5, EventPhase::CHANGE);
  Inject(5, 5, EventPhase::CHANGE);
  Inject(5, 0, EventPhase::CHANGE);
  RunLoopUntil([&child_events] { return child_events.size() == 4u; });  // Succeeds or times out.

  {  // Target should receive all events rotated 90 degrees.
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER(child_events[0].pointer_sample(), viewport_to_view_transform, 0.f, 5.f);
    ASSERT_TRUE(child_events[0].has_stream_info());
    EXPECT_EQ(child_events[0].stream_info().status, MouseViewStatus::ENTERED);
    EXPECT_EQ_POINTER(child_events[1].pointer_sample(), viewport_to_view_transform, 5.f, 5.f);
    EXPECT_FALSE(child_events[1].has_stream_info());
    EXPECT_EQ_POINTER(child_events[2].pointer_sample(), viewport_to_view_transform, 5.f, 0.f);
    EXPECT_FALSE(child_events[2].has_stream_info());
    EXPECT_EQ_POINTER(child_events[3].pointer_sample(), viewport_to_view_transform, 0.f, 0.f);
    EXPECT_FALSE(child_events[3].has_stream_info());
  }
}

// Basic scene (no transformations) where the Viewport is smaller than the Views.
// We then inject two streams: The first has an ADD outside the Viewport, which counts as a miss and
// should not be seen by anyone. The second stream has the ADD inside the Viewport and subsequent
// events outside, and this full stream should be seen by the target.
TEST_F(FlatlandMouseIntegrationTest, InjectionOutsideViewport_ShouldLimitOnClick) {
  // Set up a scene with two ViewHolders, one a child of the other. Make the Views bigger than the
  // Viewport.
  FlatlandPtr child_instance;
  MouseSourcePtr child_mouse_source;
  ViewRefFocusedPtr child_focused_ptr;

  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_instance, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Scene is now set up, send in the input. The initial click is outside the viewport and
  // the stream should therefore not be seen by anyone.
  const uint8_t kButtonId = 1;
  const std::vector<uint8_t> button_vec = {kButtonId};
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, button_vec, kIdentityMatrix);

  // Set the viewport to only be the top-left quadrant of the screen.
  Viewport viewport;
  viewport.set_extents({{{0, 0}, {display_width_ / 2, display_height_ / 2}}});
  viewport.set_viewport_to_context_transform(kIdentityMatrix);
  InjectNewViewport(std::move(viewport));

  Inject(display_width_, display_height_, EventPhase::ADD,
         button_vec);  // Outside viewport. Button down.
  // Remainder inside viewport, but should not be delivered.
  Inject(5, 0, EventPhase::CHANGE, button_vec);
  Inject(5, 5, EventPhase::CHANGE, button_vec);
  Inject(0, 5, EventPhase::CHANGE);  // Button up. Hover event should be delivered.

  // Send in button down starting in the viewport and moving outside.
  Inject(1, 1, EventPhase::CHANGE, button_vec);  // Inside viewport.
  // Remainder outside viewport, but should still be delivered.
  Inject(display_width_, 0, EventPhase::CHANGE, button_vec);
  Inject(display_width_, display_height_, EventPhase::CHANGE, button_vec);
  Inject(0, display_height_, EventPhase::CHANGE, button_vec);
  Inject(1, 1, EventPhase::CHANGE);  // Inside viewport. Button up.
  RunLoopUntil([&child_events] { return child_events.size() == 6u; });  // Succeeds or times out.

  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER_WITH_BUTTONS(child_events[0].pointer_sample(), viewport_to_view_transform,
                                   0.f, 5.f, std::vector<uint8_t>());
    EXPECT_EQ_POINTER_WITH_BUTTONS(child_events[1].pointer_sample(), viewport_to_view_transform,
                                   1.f, 1.f, button_vec);
    EXPECT_EQ_POINTER_WITH_BUTTONS(child_events[2].pointer_sample(), viewport_to_view_transform,
                                   display_width_, 0.f, button_vec);
    EXPECT_EQ_POINTER_WITH_BUTTONS(child_events[3].pointer_sample(), viewport_to_view_transform,
                                   display_width_, display_height_, button_vec);
    EXPECT_EQ_POINTER_WITH_BUTTONS(child_events[4].pointer_sample(), viewport_to_view_transform,
                                   0.f, display_height_, button_vec);
    EXPECT_EQ_POINTER_WITH_BUTTONS(child_events[5].pointer_sample(), viewport_to_view_transform,
                                   1.f, 1.f, std::vector<uint8_t>());
  }
}

TEST_F(FlatlandMouseIntegrationTest, HoverTest) {
  FlatlandPtr child_instance;
  MouseSourcePtr child_mouse_source;
  ViewRefFocusedPtr child_focused_ptr;

  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_instance, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  // Scene is now set up, send in the input. The initial click is outside the viewport and
  // the stream should therefore not be seen by anyone.
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, {}, kIdentityMatrix);

  // Set the viewport to only be the top-left 9x9 section of the screen.
  Viewport viewport;
  viewport.set_extents({{{0, 0}, {9, 9}}});
  viewport.set_viewport_to_context_transform(kIdentityMatrix);
  InjectNewViewport(std::move(viewport));
  // Outside viewport.
  Inject(10, 10, EventPhase::ADD);
  // Inside viewport.
  Inject(5, 0, EventPhase::CHANGE);  // "View entered".
  Inject(5, 5, EventPhase::CHANGE);
  Inject(0, 5, EventPhase::CHANGE);
  // Outside viewport.
  Inject(50, 0, EventPhase::CHANGE);  // "View exited".
  Inject(50, 50, EventPhase::CHANGE);
  Inject(0, 50, EventPhase::CHANGE);
  // Inside viewport.
  Inject(1, 1, EventPhase::CHANGE);  // "View entered".

  RunLoopUntil([&child_events] { return child_events.size() == 5u; });  // Succeeds or times out.

  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    {
      const auto& event = child_events[0];
      EXPECT_EQ_POINTER(event.pointer_sample(), viewport_to_view_transform, 5.f, 0.f);
      ASSERT_TRUE(event.has_stream_info());
      EXPECT_EQ(event.stream_info().status, MouseViewStatus::ENTERED);
    }
    {
      const auto& event = child_events[1];
      EXPECT_EQ_POINTER(event.pointer_sample(), viewport_to_view_transform, 5.f, 5.f);
      EXPECT_FALSE(event.has_stream_info());
    }
    {
      const auto& event = child_events[2];
      EXPECT_EQ_POINTER(event.pointer_sample(), viewport_to_view_transform, 0.f, 5.f);
      EXPECT_FALSE(event.has_stream_info());
    }
    {
      const auto& event = child_events[3];
      EXPECT_FALSE(event.has_pointer_sample(), "Should get no pointer sample on View Exit");
      ASSERT_TRUE(event.has_stream_info());
      EXPECT_EQ(event.stream_info().status, MouseViewStatus::EXITED);
    }
    {
      const auto& event = child_events[4];
      EXPECT_EQ_POINTER(event.pointer_sample(), viewport_to_view_transform, 1.f, 1.f);
      ASSERT_TRUE(event.has_stream_info());
      EXPECT_EQ(event.stream_info().status, MouseViewStatus::ENTERED);
    }
  }
}

TEST_F(FlatlandMouseIntegrationTest, InjectorDeath_ShouldCauseViewExitedEvent) {
  FlatlandPtr child_instance;
  MouseSourcePtr child_mouse_source;
  ViewRefFocusedPtr child_focused_ptr;

  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_instance, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, {}, kIdentityMatrix);

  Inject(2.5f, 2.5f, EventPhase::ADD);  // "View entered".

  // Register another injector, killing the old channel.
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, {}, kIdentityMatrix);

  RunLoopUntil([&child_events] { return child_events.size() == 2u; });  // Succeeds or times out.

  {
    const auto& event = child_events[0];
    EXPECT_TRUE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::ENTERED);
  }
  {
    const auto& event = child_events[1];
    EXPECT_FALSE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::EXITED);
  }
}

TEST_F(FlatlandMouseIntegrationTest, REMOVEandCANCEL_ShouldCauseViewExitedEvents) {
  FlatlandPtr child_instance;
  MouseSourcePtr child_mouse_source;
  ViewRefFocusedPtr child_focused_ptr;

  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });

  auto child_view_ref = CreateChildView(child_instance, child_mouse_source.NewRequest(),
                                        child_focused_ptr.NewRequest());

  // Listen for input events.
  std::vector<MouseEvent> child_events;
  StartWatchLoop(child_mouse_source, child_events);

  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, {}, kIdentityMatrix);

  Inject(2.5f, 2.5f, EventPhase::ADD);     // "View entered".
  Inject(2.5f, 2.5f, EventPhase::REMOVE);  // "View exited".

  RunLoopUntil([&child_events] { return child_events.size() == 2u; });  // Succeeds or times out.

  {
    const auto& event = child_events[0];
    EXPECT_TRUE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::ENTERED);
  }
  {
    const auto& event = child_events[1];
    EXPECT_FALSE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::EXITED);
  }

  child_events.clear();
  Inject(2.5f, 2.5f, EventPhase::ADD);     // "View entered".
  Inject(2.5f, 2.5f, EventPhase::CANCEL);  // "View exited".

  RunLoopUntil([&child_events] { return child_events.size() == 2u; });  // Succeeds or times out.

  {
    const auto& event = child_events[0];
    EXPECT_TRUE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::ENTERED);
  }
  {
    const auto& event = child_events[1];
    EXPECT_FALSE(event.has_pointer_sample());
    ASSERT_TRUE(event.has_stream_info());
    EXPECT_EQ(event.stream_info().status, MouseViewStatus::EXITED);
  }
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
  FlatlandPtr parent_instance;
  MouseSourcePtr parent_mouse_source;

  parent_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  parent_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  const auto parent_view_ref = CreateChildView(parent_instance, parent_mouse_source.NewRequest());

  FlatlandPtr child_instance = realm_->Connect<Flatland>();
  child_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });

  {
    // Set up the anonymous child view.
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    child_instance->CreateView(std::move(child_token), parent_viewport_watcher.NewRequest());
    child_instance->CreateTransform(kRootTransform);
    child_instance->SetRootTransform(kRootTransform);
    BlockingPresent(child_instance);

    // Attach it to the parent.
    const TransformId viewport_transform{.value = 2};
    const ContentId parent_content{.value = 1};
    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    ViewportProperties properties;
    properties.set_logical_size({kDefaultSize, kDefaultSize});
    parent_instance->CreateTransform(viewport_transform);
    parent_instance->CreateViewport(parent_content, std::move(parent_token), std::move(properties),
                                    child_view_watcher.NewRequest());
    parent_instance->SetContent(viewport_transform, parent_content);
    parent_instance->AddChild(kRootTransform, viewport_transform);
    BlockingPresent(parent_instance);
  }

  // Create the named grandchild view along with its mouse source and attach it to the child.
  FlatlandPtr grandchild_instance;
  MouseSourcePtr grandchild_mouse_source;
  grandchild_instance.set_error_handler([](zx_status_t status) {
    FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
  });
  grandchild_mouse_source.set_error_handler([](zx_status_t status) {
    FAIL("Mouse source closed with status: %s", zx_status_get_string(status));
  });
  CreateAndAddChildView(child_instance, /*parent_transform=*/{.value = 2}, kRootTransform,
                        /*parent_content=*/{.value = 2}, grandchild_instance,
                        grandchild_mouse_source.NewRequest());

  // Listen for mouse events.
  std::vector<MouseEvent> parent_events;
  StartWatchLoop(parent_mouse_source, parent_events);
  std::vector<MouseEvent> grandchild_events;
  StartWatchLoop(grandchild_mouse_source, grandchild_events);

  // Inject an input event at (0,0) which should hit every view. The anonymous child tree should be
  // ignored and the parent should receive it.
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(parent_view_ref),
                   DispatchPolicy::MOUSE_HOVER_AND_LATCH_IN_TARGET, {}, kIdentityMatrix);
  Inject(0, 0, EventPhase::ADD);
  RunLoopUntil([&parent_events] { return parent_events.size() == 1u; });
  EXPECT_TRUE(parent_events[0].has_pointer_sample());
  EXPECT_TRUE(grandchild_events.empty());
}

}  // namespace integration_tests
