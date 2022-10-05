// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/pointer/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <lib/async-loop/testing/cpp/real_loop.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <zircon/status.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <zxtest/zxtest.h>

#include "src/ui/scenic/tests/utils/scenic_realm_builder.h"
#include "src/ui/scenic/tests/utils/utils.h"

// These tests exercise the integration between Flatland and the InputSystem, including the
// View-to-View transform logic between the injection point and the receiver.
// Setup:
// - The test fixture sets up the display + the root session and view.
// - Injection done in context View Space, with fuchsia.ui.pointerinjector
// - Target(s) specified by View (using view ref koids)
// - Dispatch done to fuchsia.ui.pointer.TouchSource in receiver View Space.

// Macro for calling EXPECT on fuchsia::ui::pointer::PointerSample.
// Is a macro to ensure we get the correct line number for the error.
#define EXPECT_EQ_POINTER(pointer_sample, viewport_to_view_transform, expected_phase, expected_x, \
                          expected_y)                                                             \
  {                                                                                               \
    EXPECT_EQ(pointer_sample.phase(), expected_phase);                                            \
    const Mat3 transform_matrix = ArrayToMat3(viewport_to_view_transform);                        \
    const std::array<float, 2> transformed_pointer =                                              \
        TransformPointerCoords(pointer_sample.position_in_viewport(), transform_matrix);          \
    EXPECT_TRUE(CmpFloatingValues(transformed_pointer[0], expected_x));                           \
    EXPECT_TRUE(CmpFloatingValues(transformed_pointer[1], expected_y));                           \
  }

namespace integration_tests {

using fuchsia::ui::pointer::TouchInteractionStatus;
using fuchsia::ui::pointerinjector::DispatchPolicy;
using fuchsia::ui::pointerinjector::Viewport;
using fupi_EventPhase = fuchsia::ui::pointerinjector::EventPhase;
using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::ContentId;
using fuchsia::ui::composition::Flatland;
using fuchsia::ui::composition::FlatlandDisplay;
using fuchsia::ui::composition::Orientation;
using fuchsia::ui::composition::ParentViewportWatcher;
using fuchsia::ui::composition::TransformId;
using fuchsia::ui::composition::ViewBoundProtocols;
using fuchsia::ui::composition::ViewportProperties;
using fuchsia::ui::pointer::EventPhase;
using fuchsia::ui::pointer::TouchEvent;
using fuchsia::ui::pointer::TouchResponse;
using fuchsia::ui::pointer::TouchResponseType;
using fuchsia::ui::views::ViewCreationToken;
using fuchsia::ui::views::ViewportCreationToken;
using fuchsia::ui::views::ViewRef;
using RealmRoot = component_testing::RealmRoot;

namespace {
std::array<float, 2> TransformPointerCoords(std::array<float, 2> pointer, const Mat3& transform) {
  const Vec3 homogenous_pointer = {pointer[0], pointer[1], 1};
  Vec3 transformed_pointer = transform * homogenous_pointer;
  FX_CHECK(transformed_pointer[2] != 0);
  const Vec3& homogenized = transformed_pointer / transformed_pointer[2];
  return {homogenized[0], homogenized[1]};
}

}  // namespace

class FlatlandTouchIntegrationTest : public zxtest::Test, public loop_fixture::RealLoop {
 protected:
  static constexpr uint32_t kDeviceId = 1111;
  static constexpr uint32_t kPointerId = 2222;
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

    flatland_display_ = realm_->Connect<fuchsia::ui::composition::FlatlandDisplay>();
    flatland_display_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
    });
    pointerinjector_registry_ = realm_->Connect<fuchsia::ui::pointerinjector::Registry>();
    pointerinjector_registry_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to pointerinjector Registry: %s", zx_status_get_string(status));
    });

    // Set up root view.
    root_session_ = realm_->Connect<fuchsia::ui::composition::Flatland>();
    root_session_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
    });

    root_session_->CreateTransform(kRootTransform);
    root_session_->SetRootTransform(kRootTransform);

    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    flatland_display_->SetContent(std::move(parent_token), child_view_watcher.NewRequest());

    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    root_view_ref_ = fidl::Clone(identity.view_ref);
    root_session_->CreateView2(std::move(child_token), std::move(identity), {},
                               parent_viewport_watcher.NewRequest());

    parent_viewport_watcher->GetLayout([this](auto layout_info) {
      ASSERT_TRUE(layout_info.has_logical_size());
      const auto [width, height] = layout_info.logical_size();
      display_width_ = static_cast<float>(width);
      display_height_ = static_cast<float>(height);
    });
    BlockingPresent(root_session_);

    // Wait until we get the display size.
    RunLoopUntil([this] { return display_width_ != 0 && display_height_ != 0; });
  }

  void BlockingPresent(fuchsia::ui::composition::FlatlandPtr& flatland) {
    bool presented = false;
    flatland.events().OnFramePresented = [&presented](auto) { presented = true; };
    flatland->Present({});
    RunLoopUntil([&presented] { return presented; });
    flatland.events().OnFramePresented = nullptr;
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

  void Inject(float x, float y, fupi_EventPhase phase) {
    fuchsia::ui::pointerinjector::Event event;
    event.set_timestamp(0);
    {
      fuchsia::ui::pointerinjector::PointerSample pointer_sample;
      pointer_sample.set_pointer_id(kPointerId);
      pointer_sample.set_phase(phase);
      pointer_sample.set_position_in_viewport({x, y});
      fuchsia::ui::pointerinjector::Data data;
      data.set_pointer_sample(std::move(pointer_sample));
      event.set_data(std::move(data));
    }
    std::vector<fuchsia::ui::pointerinjector::Event> events;
    events.emplace_back(std::move(event));
    bool hanging_get_returned = false;
    injector_->Inject(std::move(events), [&hanging_get_returned] { hanging_get_returned = true; });
    RunLoopUntil(
        [this, &hanging_get_returned] { return hanging_get_returned || injector_channel_closed_; });
  }

  void RegisterInjector(fuchsia::ui::views::ViewRef context_view_ref,
                        fuchsia::ui::views::ViewRef target_view_ref,
                        DispatchPolicy dispatch_policy = DispatchPolicy::EXCLUSIVE_TARGET,
                        std::array<float, 9> viewport_to_context_transform = kIdentityMatrix) {
    fuchsia::ui::pointerinjector::Config config;
    config.set_device_id(kDeviceId);
    config.set_device_type(fuchsia::ui::pointerinjector::DeviceType::TOUCH);
    config.set_dispatch_policy(dispatch_policy);
    {
      {
        fuchsia::ui::pointerinjector::Context context;
        context.set_view(std::move(context_view_ref));
        config.set_context(std::move(context));
      }
      {
        fuchsia::ui::pointerinjector::Target target;
        target.set_view(std::move(target_view_ref));
        config.set_target(std::move(target));
      }
      {
        fuchsia::ui::pointerinjector::Viewport viewport;
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
    ASSERT_FALSE(injector_channel_closed_);
  }

  // Starts a recursive TouchSource::Watch() loop that collects all received events into
  // |out_events|.
  void StartWatchLoop(fuchsia::ui::pointer::TouchSourcePtr& touch_source,
                      std::vector<TouchEvent>& out_events,
                      TouchResponseType response_type = TouchResponseType::MAYBE) {
    const size_t index = watch_loops_.size();
    watch_loops_.emplace_back();
    watch_loops_.at(index) = [this, &touch_source, &out_events, response_type,
                              index](std::vector<TouchEvent> events) {
      std::vector<TouchResponse> responses;
      for (auto& event : events) {
        if (event.has_pointer_sample()) {
          TouchResponse response;
          response.set_response_type(response_type);
          responses.emplace_back(std::move(response));
        } else {
          responses.emplace_back();
        }
      }
      std::move(events.begin(), events.end(), std::back_inserter(out_events));

      touch_source->Watch(std::move(responses), [this, index](std::vector<TouchEvent> events) {
        watch_loops_.at(index)(std::move(events));
      });
    };
    touch_source->Watch({}, watch_loops_.at(index));
  }

  void ConnectChildView(fuchsia::ui::composition::FlatlandPtr& flatland,
                        ViewportCreationToken&& token, fuchsia::math::SizeU size,
                        TransformId transform_id, ContentId content_id) {
    // Let the client_end die.
    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    ViewportProperties properties;

    FX_CHECK(display_width_ > 0 && display_height_ > 0);
    properties.set_logical_size(size);

    flatland->CreateTransform(transform_id);
    flatland->AddChild(kRootTransform, transform_id);

    flatland->CreateViewport(content_id, std::move(token), std::move(properties),
                             child_view_watcher.NewRequest());
    flatland->SetContent(transform_id, content_id);

    BlockingPresent(flatland);
  }

  // Injects |points| and checks that the events received in |view_events| match with an offset.
  void InjectionHelper(const std::vector<std::array<float, 2>>& points,
                       const std::vector<TouchEvent>& view_events, float x_offset, float y_offset) {
    if (points.size() == 0)
      return;

    for (size_t i = 0; i < points.size(); ++i) {
      auto phase =
          i == 0 ? fupi_EventPhase::ADD
                 : (i == points.size() - 1 ? fupi_EventPhase::REMOVE : fupi_EventPhase::CHANGE);
      Inject(points[i][0], points[i][1], phase);
    }

    RunLoopUntil([&view_events, num_points = points.size()] {
      // Depending on contest results there may be a TouchInteractionResult appended to
      // |view_events|.
      return view_events.size() >= num_points;
    });  // Succeeds or times out.

    const auto& viewport_to_view_transform =
        view_events[0].view_parameters().viewport_to_view_transform;

    for (size_t i = 0; i < points.size(); ++i) {
      auto phase = i == 0 ? EventPhase::ADD
                          : (i == points.size() - 1 ? EventPhase::REMOVE : EventPhase::CHANGE);

      EXPECT_EQ_POINTER(view_events[i].pointer_sample(), viewport_to_view_transform, phase,
                        points[i][0] + x_offset, points[i][1] + y_offset);
    }
  }

  const TransformId kRootTransform{.value = 1};
  const ContentId kRootContentId{.value = 1};

  fuchsia::math::SizeU FullscreenSize() {
    return {static_cast<uint32_t>(display_width_), static_cast<uint32_t>(display_height_)};
  }

  bool injector_channel_closed_ = false;
  float display_width_ = 0;
  float display_height_ = 0;

  std::array<std::array<float, 2>, 2> FullScreenExtents() const {
    return {{{0, 0}, {display_width_, display_height_}}};
  }

  fuchsia::ui::composition::FlatlandPtr root_session_;
  fuchsia::ui::views::ViewRef root_view_ref_;
  std::unique_ptr<RealmRoot> realm_;

 private:
  fuchsia::ui::composition::FlatlandDisplayPtr flatland_display_;
  fuchsia::ui::pointerinjector::RegistryPtr pointerinjector_registry_;
  fuchsia::ui::pointerinjector::DevicePtr injector_;

  // Holds watch loops so they stay alive through the duration of the test.
  std::vector<std::function<void(std::vector<TouchEvent>)>> watch_loops_;
};

// This test sets up a scene with no transformations. Injected events should go straight through to
// the child unchanged.
TEST_F(FlatlandTouchIntegrationTest, BasicInputTest) {
  fuchsia::ui::composition::FlatlandPtr child_session;
  fuchsia::ui::pointer::TouchSourcePtr child_touch_source;
  child_session = realm_->Connect<fuchsia::ui::composition::Flatland>();
  child_touch_source.set_error_handler([](zx_status_t status) {
    FAIL("Touch source closed with status: %s", zx_status_get_string(status));
  });

  // Set up the root graph.
  auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
  TransformId kTransformId = {.value = 2};
  ConnectChildView(root_session_, std::move(parent_token), FullscreenSize(), kTransformId,
                   kRootContentId);

  // Set up the child view and its TouchSource channel.
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  auto identity = scenic::NewViewIdentityOnCreation();
  auto child_view_ref = fidl::Clone(identity.view_ref);
  fuchsia::ui::composition::ViewBoundProtocols protocols;
  protocols.set_touch_source(child_touch_source.NewRequest());
  child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                             parent_viewport_watcher.NewRequest());
  const TransformId kTransform{.value = 42};
  child_session->CreateTransform(kTransform);
  child_session->SetRootTransform(kTransform);
  BlockingPresent(child_session);

  // Listen for input events.
  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Scene is now set up, send in the input. One event for each corner of the view.
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);

  Inject(display_width_, display_height_, fupi_EventPhase::ADD);
  Inject(display_width_, 0, fupi_EventPhase::CHANGE);
  Inject(0, 0, fupi_EventPhase::CHANGE);
  Inject(0, display_height_, fupi_EventPhase::REMOVE);

  RunLoopUntil([&child_events] { return child_events.size() == 4u; });  // Succeeds or times out.

  // Target should receive identical events to injected, since their coordinate spaces are the same.
  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER(child_events[0].pointer_sample(), viewport_to_view_transform, EventPhase::ADD,
                      display_width_, display_height_);
    EXPECT_EQ_POINTER(child_events[1].pointer_sample(), viewport_to_view_transform,
                      EventPhase::CHANGE, display_width_, 0.f);
    EXPECT_EQ_POINTER(child_events[2].pointer_sample(), viewport_to_view_transform,
                      EventPhase::CHANGE, 0.f, 0.f);
    EXPECT_EQ_POINTER(child_events[3].pointer_sample(), viewport_to_view_transform,
                      EventPhase::REMOVE, 0.f, display_height_);
  }
}

// With a smaller viewport than the context view, test for two things:
//
// 1) Touches starting *outside* the viewport should miss completely
// 2) Touches starting *inside* the viewport and then leaving the viewport should all be delivered
TEST_F(FlatlandTouchIntegrationTest, ViewportSmallerThanContextView) {
  fuchsia::ui::composition::FlatlandPtr child_session;
  fuchsia::ui::pointer::TouchSourcePtr child_touch_source;
  child_session = realm_->Connect<fuchsia::ui::composition::Flatland>();
  child_touch_source.set_error_handler([](zx_status_t status) {
    FAIL("Touch source closed with status: %s", zx_status_get_string(status));
  });

  // Set up the root graph.
  auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
  TransformId kTransformId = {.value = 2};
  ConnectChildView(root_session_, std::move(parent_token), FullscreenSize(), kTransformId,
                   kRootContentId);

  // Set up the child view and its TouchSource channel.
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  auto identity = scenic::NewViewIdentityOnCreation();
  auto child_view_ref = fidl::Clone(identity.view_ref);
  fuchsia::ui::composition::ViewBoundProtocols protocols;
  protocols.set_touch_source(child_touch_source.NewRequest());
  child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                             parent_viewport_watcher.NewRequest());
  const TransformId kTransform{.value = 42};
  child_session->CreateTransform(kTransform);
  child_session->SetRootTransform(kTransform);
  BlockingPresent(child_session);

  // Listen for input events.
  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Scene is now set up, send in the input. One event for each corner of the view.
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);

  // Set the viewport to only be the top-left quadrant of the screen.
  Viewport viewport;
  viewport.set_extents({{{0, 0}, {display_width_ / 2, display_height_ / 2}}});
  viewport.set_viewport_to_context_transform(kIdentityMatrix);
  InjectNewViewport(std::move(viewport));

  // Start a touch event stream outside of the viewport. These 4 events should not be received.
  Inject(display_width_, display_height_, fupi_EventPhase::ADD);
  Inject(0, 0, fupi_EventPhase::CHANGE);
  Inject(display_width_, 0, fupi_EventPhase::CHANGE);
  Inject(0, display_height_, fupi_EventPhase::REMOVE);

  // Start a touch event stream inside of the viewport, and even the events outside of the viewport
  // should still be delivered.
  Inject(0, 0, fupi_EventPhase::ADD);
  Inject(display_width_, 0, fupi_EventPhase::CHANGE);
  Inject(display_width_, display_height_, fupi_EventPhase::CHANGE);
  Inject(0, display_height_, fupi_EventPhase::REMOVE);

  // Although 8 events were injected, only the latter 4 should be delivered.
  RunLoopUntil([&child_events] { return child_events.size() == 4u; });  // Succeeds or times out.

  // Target should receive identical events to injected, since their coordinate spaces are the same.
  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER(child_events[0].pointer_sample(), viewport_to_view_transform, EventPhase::ADD,
                      0.f, 0.f);
    EXPECT_EQ_POINTER(child_events[1].pointer_sample(), viewport_to_view_transform,
                      EventPhase::CHANGE, display_width_, 0.f);
    EXPECT_EQ_POINTER(child_events[2].pointer_sample(), viewport_to_view_transform,
                      EventPhase::CHANGE, display_width_, display_height_);
    EXPECT_EQ_POINTER(child_events[3].pointer_sample(), viewport_to_view_transform,
                      EventPhase::REMOVE, 0.f, display_height_);
  }
}

TEST_F(FlatlandTouchIntegrationTest, DisconnectTargetView_TriggersChannelClosure) {
  fuchsia::ui::composition::FlatlandPtr child_session;
  fuchsia::ui::pointer::TouchSourcePtr child_touch_source;
  child_session = realm_->Connect<fuchsia::ui::composition::Flatland>();
  child_touch_source.set_error_handler([](zx_status_t status) {
    FAIL("Touch source closed with status: %s", zx_status_get_string(status));
  });

  // Set up the root graph.
  auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
  TransformId kTransformId = {.value = 2};
  ConnectChildView(root_session_, std::move(parent_token), FullscreenSize(), kTransformId,
                   kRootContentId);

  // Set up the child view and its TouchSource channel.
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  auto identity = scenic::NewViewIdentityOnCreation();
  auto child_view_ref = fidl::Clone(identity.view_ref);
  fuchsia::ui::composition::ViewBoundProtocols protocols;
  protocols.set_touch_source(child_touch_source.NewRequest());
  child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                             parent_viewport_watcher.NewRequest());
  const TransformId kTransform{.value = 42};
  child_session->CreateTransform(kTransform);
  child_session->SetRootTransform(kTransform);
  BlockingPresent(child_session);

  // Listen for input events.
  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Scene is now set up, send in the input. One event for each corner of the view.
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);

  // Break the scene graph relation that the pointerinjector relies on. Observe the channel close
  // (lazily).
  child_session->ReleaseView();
  BlockingPresent(child_session);

  // Inject an event to trigger the channel closure.
  Inject(0, 0, fupi_EventPhase::ADD);
  RunLoopUntil([this] { return injector_channel_closed_; });  // Succeeds or times out.
}

// In this test we set up the context and the target. We apply a scale, rotation and translation
// transform to both of their viewports, and then inject pointer events to confirm that
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
TEST_F(FlatlandTouchIntegrationTest, TargetViewWithScaleRotationTranslation) {
  fuchsia::ui::composition::FlatlandPtr child_session;
  fuchsia::ui::pointer::TouchSourcePtr child_touch_source;
  child_session = realm_->Connect<fuchsia::ui::composition::Flatland>();
  child_touch_source.set_error_handler([](zx_status_t status) {
    FAIL("Touch source closed with status: %s", zx_status_get_string(status));
  });

  // Set up the root graph.
  auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
  TransformId kTransformId = {.value = 2};
  ConnectChildView(root_session_, std::move(parent_token), FullscreenSize(), kTransformId,
                   kRootContentId);

  // Set up the child view and its TouchSource channel.
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  auto identity = scenic::NewViewIdentityOnCreation();
  auto child_view_ref = fidl::Clone(identity.view_ref);
  fuchsia::ui::composition::ViewBoundProtocols protocols;
  protocols.set_touch_source(child_touch_source.NewRequest());
  child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                             parent_viewport_watcher.NewRequest());
  child_session->CreateTransform(kRootTransform);
  child_session->SetRootTransform(kRootTransform);
  BlockingPresent(child_session);

  // Scale, rotate, and translate the child_session. Those operations are applied in that order.
  root_session_->SetScale(kTransformId, {2, 3});
  root_session_->SetOrientation(kTransformId, Orientation::CCW_270_DEGREES);
  root_session_->SetTranslation(kTransformId, {1, 0});
  BlockingPresent(root_session_);

  // Listen for input events.
  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Scene is now set up, send in the input. One event for each corner of the view.
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);

  Inject(0, 0, fupi_EventPhase::ADD);
  Inject(10, 0, fupi_EventPhase::CHANGE);
  Inject(0, 10, fupi_EventPhase::CHANGE);
  Inject(10, 10, fupi_EventPhase::REMOVE);

  RunLoopUntil([&child_events] { return child_events.size() == 4u; });  // Succeeds or times out.

  // For a CCW_270 rotation, the new x' and y' from x and y is:
  // x' = y
  // y' = -x
  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER(child_events[0].pointer_sample(), viewport_to_view_transform, EventPhase::ADD,
                      0.f / 2.f, (0.f + 1.f) / 3.f);
    EXPECT_EQ_POINTER(child_events[1].pointer_sample(), viewport_to_view_transform,
                      EventPhase::CHANGE, 0.f / 2.f, (-10.f + 1.f) / 3.f);
    EXPECT_EQ_POINTER(child_events[2].pointer_sample(), viewport_to_view_transform,
                      EventPhase::CHANGE, 10.f / 2.f, (0.f + 1.f) / 3.f);
    EXPECT_EQ_POINTER(child_events[3].pointer_sample(), viewport_to_view_transform,
                      EventPhase::REMOVE, 10.f / 2.f, (-10.f + 1.f) / 3.f);
  }
}

// Create a 10x10 root view, and 10x10 child view.
//
// Rotate the child 90 degrees and ensure that touches starting on each corner get delivered. This
// confirms that small floating point deviations don't cause issues.
TEST_F(FlatlandTouchIntegrationTest, InjectedInput_OnRotatedChild_ShouldHitEdges) {
  fuchsia::ui::composition::FlatlandPtr child_session;
  fuchsia::ui::pointer::TouchSourcePtr child_touch_source;
  child_session = realm_->Connect<fuchsia::ui::composition::Flatland>();
  child_touch_source.set_error_handler([](zx_status_t status) {
    FAIL("Touch source closed with status: %s", zx_status_get_string(status));
  });

  // Set up the root graph.
  auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
  TransformId kTransformId = {.value = 2};
  ConnectChildView(root_session_, std::move(parent_token), FullscreenSize(), kTransformId,
                   kRootContentId);

  // Set up the child view and its TouchSource channel.
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  auto identity = scenic::NewViewIdentityOnCreation();
  auto child_view_ref = fidl::Clone(identity.view_ref);
  fuchsia::ui::composition::ViewBoundProtocols protocols;
  protocols.set_touch_source(child_touch_source.NewRequest());
  child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                             parent_viewport_watcher.NewRequest());
  child_session->CreateTransform(kRootTransform);
  child_session->SetRootTransform(kRootTransform);
  BlockingPresent(child_session);

  // Rotate the transform holding the child session and then translate it back into position.
  root_session_->SetOrientation(kTransformId, Orientation::CCW_270_DEGREES);
  root_session_->SetTranslation(kTransformId, {10, 0});

  {
    // Clip the root session.
    fuchsia::math::Rect rect = {0, 0, 10, 10};
    root_session_->SetClipBoundary(kRootTransform,
                                   std::make_unique<fuchsia::math::Rect>(std::move(rect)));
  }
  {
    // Clip the child session.
    fuchsia::math::Rect rect = {0, 0, 10, 10};
    root_session_->SetClipBoundary(kTransformId,
                                   std::make_unique<fuchsia::math::Rect>(std::move(rect)));
  }

  BlockingPresent(root_session_);
  BlockingPresent(child_session);

  // Listen for input events.
  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Scene is now set up, send in the input. One event for each corner of the view.
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);

  Inject(0, 0, fupi_EventPhase::ADD);
  Inject(0, 0, fupi_EventPhase::REMOVE);

  Inject(10, 0, fupi_EventPhase::ADD);
  Inject(10, 0, fupi_EventPhase::REMOVE);

  Inject(0, 10, fupi_EventPhase::ADD);
  Inject(0, 10, fupi_EventPhase::REMOVE);

  Inject(10, 10, fupi_EventPhase::ADD);
  Inject(10, 10, fupi_EventPhase::REMOVE);

  RunLoopUntil([&child_events] { return child_events.size() == 8u; });  // Succeeds or times out.

  {
    const auto& viewport_to_view_transform =
        child_events[0].view_parameters().viewport_to_view_transform;
    EXPECT_EQ_POINTER(child_events[0].pointer_sample(), viewport_to_view_transform, EventPhase::ADD,
                      0.f, 10.f);
    EXPECT_EQ_POINTER(child_events[1].pointer_sample(), viewport_to_view_transform,
                      EventPhase::REMOVE, 0.f, 10.f);

    EXPECT_EQ_POINTER(child_events[2].pointer_sample(), viewport_to_view_transform, EventPhase::ADD,
                      0.f, 0.f);
    EXPECT_EQ_POINTER(child_events[3].pointer_sample(), viewport_to_view_transform,
                      EventPhase::REMOVE, 0.f, 0.f);

    EXPECT_EQ_POINTER(child_events[4].pointer_sample(), viewport_to_view_transform, EventPhase::ADD,
                      10.f, 10.f);
    EXPECT_EQ_POINTER(child_events[5].pointer_sample(), viewport_to_view_transform,
                      EventPhase::REMOVE, 10.f, 10.f);

    EXPECT_EQ_POINTER(child_events[6].pointer_sample(), viewport_to_view_transform, EventPhase::ADD,
                      10.f, 0.f);
    EXPECT_EQ_POINTER(child_events[7].pointer_sample(), viewport_to_view_transform,
                      EventPhase::REMOVE, 10.f, 0.f);
  }
}

// This test creates a view tree of the form:
//
//    root_view
//        |
//   parent_view
//     /      \
// child_A  child_B
//
// Where the root and parent are full screen, and child_A and child_B are partial screen views with
// some overlap in the middle. Child A is "A"bove child B, who is "B"elow.
//
//
// Let width and height represent the display width and height. Consider the following diagram,
// drawn mostly to scale.
//
// There are two full screen views: root (context) and parent (target). For event streams 1, 2, and
// 4, the viewport is full screen. For #4 the viewport is full screen but scaled, see that part of
// the code for more details.
//
// The top-left point of A is (width / 4, height / 4). The top-left point of
// B is (width/2, height/4).Both A and B have the same dimensions: [width / 2 x height x 2].
// Partial screen views: A on the left, B on the right, with A and B overlapping in the middle.
// -------------------------------------
// |Root/Parent/Viewport               |
// |                                   |
// |        ---------------------------|
// |        |A       | <AB>   |       B|
// |        |        |        |        |
// |        |        |        |        |
// |        ---------------------------|
// |                                   |
// |                                   |
// -------------------------------------
//
//
// Diagram for event stream #3 with a transformed viewport (VP):
// Top-left point of the viewport is the same as A's top-left point. Bottom-right point of the
// viewport is context view's bottom-right point.
// -------------------------------------
// |Root/Parent                        |
// |                                   |
// |        ---------------------------|
// |        |A/VP    | <AB>/VP|    B/VP|
// |        |        |        |        |
// |        |        |        |        |
// |        |- - - - - - - - - - - - - |
// |        |                          |
// |        |                  Viewport|
// -------------------------------------
TEST_F(FlatlandTouchIntegrationTest, PartialScreenOverlappingViews) {
  fuchsia::ui::composition::FlatlandPtr parent_session;
  fuchsia::ui::pointer::TouchSourcePtr parent_touch_source;
  parent_touch_source.set_error_handler([](zx_status_t status) {
    FAIL("Touch source closed with status: %s", zx_status_get_string(status));
  });

  // Create the parent view and attach it to |root_session_|. Register the parent view to receive
  // input events.
  {
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    parent_session = realm_->Connect<fuchsia::ui::composition::Flatland>();
    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    ViewBoundProtocols protocols;
    protocols.set_touch_source(parent_touch_source.NewRequest());
    auto identity = scenic::NewViewIdentityOnCreation();
    auto parent_view_ref = fidl::Clone(identity.view_ref);

    TransformId kTransformId = {.value = 2};
    ConnectChildView(
        root_session_, std::move(parent_token),
        {static_cast<uint32_t>(display_width_), static_cast<uint32_t>(display_height_)},
        kTransformId, kRootContentId);

    parent_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                                parent_viewport_watcher.NewRequest());

    parent_session->CreateTransform(kRootTransform);
    parent_session->SetRootTransform(kRootTransform);

    // The parent's Present call generates a snapshot which includes the ViewRef.
    BlockingPresent(parent_session);
    RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(parent_view_ref),
                     DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);
  }

  auto [child_token_A, parent_token_A] = scenic::ViewCreationTokenPair::New();
  auto [child_token_B, parent_token_B] = scenic::ViewCreationTokenPair::New();
  TransformId kTransformId_A = {.value = 2};
  TransformId kTransformId_B = {.value = 3};
  ContentId kContent_A = {.value = 2};
  ContentId kContent_B = {.value = 3};

  // Create child view A.
  fuchsia::ui::composition::FlatlandPtr child_session_A;
  fuchsia::ui::pointer::TouchSourcePtr child_A_touch_source;
  child_session_A = realm_->Connect<fuchsia::ui::composition::Flatland>();
  child_A_touch_source.set_error_handler([](zx_status_t status) {
    FAIL("Touch source A closed with status: %s", zx_status_get_string(status));
  });

  // Create child view B.
  fuchsia::ui::composition::FlatlandPtr child_session_B;
  fuchsia::ui::pointer::TouchSourcePtr child_B_touch_source;
  child_session_B = realm_->Connect<fuchsia::ui::composition::Flatland>();
  child_B_touch_source.set_error_handler([](zx_status_t status) {
    FAIL("Touch source B closed with status: %s", zx_status_get_string(status));
  });

  // Define A and B width and height as half of the display width and height.
  uint32_t half_width = static_cast<int32_t>(display_width_) / 2;
  uint32_t half_height = static_cast<int32_t>(display_height_) / 2;

  // "A" should be connected after "B", since the topologically-last view is highest in paint order,
  // and therefore above its sibling views.
  ConnectChildView(parent_session, std::move(parent_token_B), {half_width, half_height},
                   kTransformId_B, kContent_B);
  ConnectChildView(parent_session, std::move(parent_token_A), {half_width, half_height},
                   kTransformId_A, kContent_A);

  // Set up child view A.
  {
    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    auto child_view_ref = fidl::Clone(identity.view_ref);
    fuchsia::ui::composition::ViewBoundProtocols protocols;
    protocols.set_touch_source(child_A_touch_source.NewRequest());
    child_session_A->CreateView2(std::move(child_token_A), std::move(identity),
                                 std::move(protocols), parent_viewport_watcher.NewRequest());
    child_session_A->CreateTransform(kRootTransform);
    child_session_A->SetRootTransform(kRootTransform);
  }

  // Set up child view B.
  {
    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    auto child_view_ref = fidl::Clone(identity.view_ref);
    fuchsia::ui::composition::ViewBoundProtocols protocols;
    protocols.set_touch_source(child_B_touch_source.NewRequest());
    child_session_B->CreateView2(std::move(child_token_B), std::move(identity),
                                 std::move(protocols), parent_viewport_watcher.NewRequest());
    child_session_B->CreateTransform(kRootTransform);
    child_session_B->SetRootTransform(kRootTransform);
  }

  // A starts at 1/4 the width of the screen, and goes until the 3/4 mark.
  int32_t view_A_x = static_cast<int32_t>(display_width_) / 4;
  int32_t view_A_width = half_width;
  int32_t view_A_y = static_cast<int32_t>(display_height_) / 4;
  int32_t view_A_height = half_height;

  // B starts at 1/2 the width of the screen, and goes until the 4/4 mark.
  //
  // This implies that there is overlap between the 1/2 and 3/4 marks of the screen.
  int32_t view_B_x = static_cast<int32_t>(display_width_) / 2;
  int32_t view_B_width = half_width;
  int32_t view_B_y = static_cast<int32_t>(display_height_) / 4;
  int32_t view_B_height = half_height;

  // Define all useful coords for convenience later.
  float A_x_min = static_cast<float>(view_A_x);
  float A_x_max = static_cast<float>(view_A_x + view_A_width);
  float A_y_min = static_cast<float>(view_A_y);
  float A_y_max = static_cast<float>(view_A_y + view_A_height);

  float B_x_min = static_cast<float>(view_B_x);
  float B_x_max = static_cast<float>(view_B_x + view_B_width);
  float B_y_min = static_cast<float>(view_B_y);
  float B_y_max = static_cast<float>(view_B_y + view_B_height);

  float A_B_height = static_cast<float>(view_A_height);
  float A_B_combined_width = B_x_max - A_x_min;

  // Ensure there's overlap with A and B.
  EXPECT_TRUE(A_x_min <= B_x_min && B_x_min <= A_x_max && A_x_max <= B_x_max);
  EXPECT_TRUE(A_y_min == B_y_min && A_y_max == B_y_max);

  parent_session->SetTranslation(kTransformId_A, {view_A_x, view_A_y});
  parent_session->SetTranslation(kTransformId_B, {view_B_x, view_B_y});

  // Commit all changes.
  BlockingPresent(parent_session);
  BlockingPresent(child_session_A);
  BlockingPresent(child_session_B);

  // Listen for input events.
  std::vector<TouchEvent> child_A_events;
  StartWatchLoop(child_A_touch_source, child_A_events);

  std::vector<TouchEvent> child_B_events;
  StartWatchLoop(child_B_touch_source, child_B_events);

  std::vector<TouchEvent> parent_events;
  StartWatchLoop(parent_touch_source, parent_events);

  /***** Setup done. Begin injecting input events into the scene. *****/

  /*
   * Event stream #1.
   */

  // Start a touch event stream in the middle of the screen, where A and B overlap. A should receive
  // the input events even as it goes from A to B and vice-versa.

  std::vector<std::array<float, 2>> points;
  points.reserve(5);

  points.push_back({B_x_min, B_y_min});
  points.push_back({A_x_min, A_y_min});
  points.push_back({B_x_max, B_y_max});
  points.push_back({B_x_max, B_y_min});
  points.push_back({A_x_min, A_y_max});

  // Translate all expected points by [-A_x_min, -A_y_min] since the viewport_to_view_transform
  // transforms points into A's coordinate space.
  InjectionHelper(points, child_A_events, -A_x_min, -A_y_min);

  // Ensure parent also received events, but not the below sibling.
  EXPECT_EQ(parent_events.size(), 6u);  // 5 events + TouchInteractionResult
  EXPECT_EQ(child_B_events.size(), 0u);

  // Reset vectors for the next stream.
  parent_events.clear();
  child_A_events.clear();
  child_B_events.clear();
  points.clear();

  /*
   * Event stream #2.
   */

  // Start a touch event stream over B. B should receive the input events even as it goes over A.

  points.reserve(5);
  points.push_back({B_x_max, B_y_max});
  points.push_back({A_x_min, A_y_min});
  points.push_back({B_x_min, B_y_min});
  points.push_back({B_x_max, B_y_min});
  points.push_back({A_x_min, A_y_max});

  InjectionHelper(points, child_B_events, -B_x_min, -B_y_min);

  // Ensure parent also received events, but not the above sibling.
  EXPECT_EQ(parent_events.size(), 6u);  // 5 events + TouchInteractionResult
  EXPECT_EQ(child_A_events.size(), 0u);

  // Reset vectors for the next stream.
  parent_events.clear();
  child_A_events.clear();
  child_B_events.clear();
  points.clear();

  /*
   * Event stream #3.
   */

  // Change the viewport size and translate it.

  // Keep the bottom-right corner of the viewport the same, and move the top-left corner to be equal
  // to view A's top-left corner.
  {
    Viewport viewport;
    viewport.set_extents({{{0, 0}, {display_width_ - A_x_min, display_height_ - A_y_min}}});
    viewport.set_viewport_to_context_transform({1, 0, 0,                // col 1
                                                0, 1, 0,                // col 2
                                                A_x_min, A_y_min, 1});  // col 3
    InjectNewViewport(std::move(viewport));
  }

  points.push_back({0, 0});
  points.push_back({A_B_combined_width, 0});
  points.push_back({A_B_combined_width, A_B_height});
  points.push_back({0, A_B_height});

  InjectionHelper(points, child_A_events, 0, 0);

  // Reset vectors for the next stream.
  parent_events.clear();
  child_A_events.clear();
  child_B_events.clear();
  points.clear();

  /*
   * Event stream #4.
   */

  // Scale the viewport to be the same size as the context view but with double the "resolution".
  // Meaning a point at (x,y) in the context coordinate space is at (2x,2y) in the viewport
  // coordinate space.

  {
    Viewport viewport;
    viewport.set_extents({{{0, 0}, {display_width_ * 2, display_height_ * 2}}});
    viewport.set_viewport_to_context_transform({0.5, 0, 0,  // col 1
                                                0, 0.5, 0,  // col 2
                                                0, 0, 1});  // col 3
    InjectNewViewport(std::move(viewport));
  }

  // Injecting a touch at (A_x_max * 2, A_y_max * 2) should actually hit A at its bottom right
  // corner, given the viewport scale changes.
  points.push_back({A_x_max, A_y_max});
  points.push_back({A_x_max, A_y_min});
  points.push_back({A_x_min, A_y_min});
  points.push_back({A_x_min, A_y_max});

  for (size_t i = 0; i < points.size(); ++i) {
    auto phase = i == 0
                     ? fupi_EventPhase::ADD
                     : (i == points.size() - 1 ? fupi_EventPhase::REMOVE : fupi_EventPhase::CHANGE);
    Inject(points[i][0] * 2, points[i][1] * 2, phase);
  }

  RunLoopUntil([&child_A_events] {
    // 4 events + TouchInteractionResult.
    return child_A_events.size() == 5u;
  });  // Succeeds or times out.

  // Offset |points| by A's top-left point.
  for (size_t i = 0; i < points.size(); ++i) {
    points[i][0] -= A_x_min;
    points[i][1] -= A_y_min;
  }

  const auto& viewport_to_view_transform =
      child_A_events[0].view_parameters().viewport_to_view_transform;
  for (size_t i = 0; i < points.size(); ++i) {
    auto phase = i == 0 ? EventPhase::ADD
                        : (i == points.size() - 1 ? EventPhase::REMOVE : EventPhase::CHANGE);

    EXPECT_EQ_POINTER(child_A_events[i].pointer_sample(), viewport_to_view_transform, phase,
                      points[i][0], points[i][1]);
  }
}

// Creates a view tree of the form
// root_view
//    |
// parent_view
//    |
// child_view
// The parent's view gets created using CreateView2 but the child's view gets created using
// CreateView. As a result, the child will not receive any input events since it does not have an
// associated ViewRef.
TEST_F(FlatlandTouchIntegrationTest, ChildCreatedUsingCreateView_DoesNotGetInput) {
  fuchsia::ui::composition::FlatlandPtr parent_session;
  fuchsia::ui::pointer::TouchSourcePtr parent_touch_source;
  parent_touch_source.set_error_handler([](zx_status_t status) {
    FAIL("Touch source closed with status: %s", zx_status_get_string(status));
  });

  // Create the parent view using CreateView2 and attach it to |root_session_|. Register the parent
  // view to receive input events.
  {
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    parent_session = realm_->Connect<fuchsia::ui::composition::Flatland>();
    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    ViewBoundProtocols protocols;
    protocols.set_touch_source(parent_touch_source.NewRequest());
    auto identity = scenic::NewViewIdentityOnCreation();
    auto parent_view_ref = fidl::Clone(identity.view_ref);

    TransformId kTransformId = {.value = 2};
    ConnectChildView(root_session_, std::move(parent_token), FullscreenSize(), kTransformId,
                     kRootContentId);

    parent_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                                parent_viewport_watcher.NewRequest());
    parent_session->CreateTransform(kRootTransform);
    parent_session->SetRootTransform(kRootTransform);

    // The parent's Present call generates a snapshot which includes the ViewRef.
    BlockingPresent(parent_session);
    RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(parent_view_ref),
                     DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);
  }

  // Create the child view using CreateView and attach it to |parent_session|.
  fuchsia::ui::composition::FlatlandPtr child_session;
  {
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    child_session = realm_->Connect<fuchsia::ui::composition::Flatland>();

    TransformId kTransformId = {.value = 2};
    ConnectChildView(parent_session, std::move(parent_token), FullscreenSize(), kTransformId,
                     kRootContentId);

    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    child_session->CreateView(std::move(child_token), parent_viewport_watcher.NewRequest());

    // The child's Present call generates a snapshot which will not include a ViewRef.
    BlockingPresent(child_session);
  }

  // Listen for input events.
  std::vector<TouchEvent> parent_events;
  StartWatchLoop(parent_touch_source, parent_events);
  // (0,0) is the origin. The child and the parent both overlap at the origin so they both are
  // eligible to receive the input event at this point.
  Inject(0, 0, fupi_EventPhase::ADD);
  RunLoopUntilIdle();

  // |parent_session| receives the input event.
  EXPECT_EQ(parent_events.size(), 1u);
}

TEST_F(FlatlandTouchIntegrationTest, ExclusiveMode_TargetDisconnectedMidStream_ShouldCancelStream) {
  fuchsia::ui::composition::FlatlandPtr child_session;
  fuchsia::ui::pointer::TouchSourcePtr child_touch_source;
  child_session = realm_->Connect<fuchsia::ui::composition::Flatland>();
  child_touch_source.set_error_handler([](zx_status_t status) {
    FAIL("Touch source closed with status: %s", zx_status_get_string(status));
  });

  // Set up the root graph.
  auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
  TransformId kTransformId = {.value = 2};
  ConnectChildView(root_session_, std::move(parent_token), FullscreenSize(), kTransformId,
                   kRootContentId);

  // Set up the child view and its TouchSource channel.
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  auto identity = scenic::NewViewIdentityOnCreation();
  auto child_view_ref = fidl::Clone(identity.view_ref);
  fuchsia::ui::composition::ViewBoundProtocols protocols;
  protocols.set_touch_source(child_touch_source.NewRequest());
  child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                             parent_viewport_watcher.NewRequest());
  const TransformId kTransform{.value = 42};
  child_session->CreateTransform(kTransform);
  child_session->SetRootTransform(kTransform);
  BlockingPresent(child_session);

  // Listen for input events.
  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Scene is now set up, send in the input.
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::EXCLUSIVE_TARGET);

  Inject(0, 0, fupi_EventPhase::ADD);
  Inject(4, 2, fupi_EventPhase::CHANGE);
  RunLoopUntil([&child_events] { return child_events.size() == 2u; });  // Succeeds or times out.

  root_session_->RemoveChild(kRootTransform, kTransformId);
  BlockingPresent(root_session_);

  // Next event should deliver a cancel event to the child (and close the injector since it's the
  // target)
  Inject(5, 5, fupi_EventPhase::CHANGE);

  RunLoopUntil([&child_events] { return child_events.size() == 3u; });  // Succeeds or times out.

  EXPECT_TRUE(injector_channel_closed_);
  EXPECT_EQ(child_events.back().pointer_sample().phase(), EventPhase::CANCEL);
}

TEST_F(FlatlandTouchIntegrationTest, ExclusiveMode_TargetDyingMidStream_ShouldKillChannel) {
  fuchsia::ui::composition::FlatlandPtr child_session;
  fuchsia::ui::pointer::TouchSourcePtr child_touch_source;
  child_session = realm_->Connect<fuchsia::ui::composition::Flatland>();
  child_touch_source.set_error_handler([](zx_status_t status) {
    FAIL("Touch source closed with status: %s", zx_status_get_string(status));
  });

  // Set up the root graph.
  auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
  TransformId kTransformId = {.value = 2};
  ConnectChildView(root_session_, std::move(parent_token), FullscreenSize(), kTransformId,
                   kRootContentId);

  // Set up the child view and its TouchSource channel.
  fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
  auto identity = scenic::NewViewIdentityOnCreation();
  auto child_view_ref = fidl::Clone(identity.view_ref);
  fuchsia::ui::composition::ViewBoundProtocols protocols;
  protocols.set_touch_source(child_touch_source.NewRequest());
  child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                             parent_viewport_watcher.NewRequest());
  const TransformId kTransform{.value = 42};
  child_session->CreateTransform(kTransform);
  child_session->SetRootTransform(kTransform);
  BlockingPresent(child_session);

  // Listen for input events.
  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Scene is now set up, send in the input.
  RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(child_view_ref),
                   DispatchPolicy::EXCLUSIVE_TARGET);

  Inject(0, 0, fupi_EventPhase::ADD);
  Inject(4, 2, fupi_EventPhase::CHANGE);
  RunLoopUntil([&child_events] { return child_events.size() == 2u; });  // Succeeds or times out.

  // Kill the target.
  child_session->CreateTransform({.value = 0});
  child_session->Present({});
  RunLoopUntil([&child_session] { return !child_session.is_bound(); });

  // TODO(fxbug.dev/110461): Present on the root session to flush the changes.
  BlockingPresent(root_session_);

  // Next event should deliver a cancel event to the child (and close the injector since it's the
  // target)
  Inject(5, 5, fupi_EventPhase::CHANGE);
  RunLoopUntil([this] { return injector_channel_closed_; });
  EXPECT_TRUE(injector_channel_closed_);
}

// Construct a scene with the following topology:
//
// Root
//   |
// Parent
//   |
// Child
//
// Injects in HitTest mode, all events delivered to Parent and Child. Then, disconnect Child and
// observe contest loss from Child.
TEST_F(FlatlandTouchIntegrationTest, HitTested_ViewDisconnectedMidContest_ShouldLoseContest) {
  fuchsia::ui::composition::FlatlandPtr parent_session;
  fuchsia::ui::pointer::TouchSourcePtr parent_touch_source;
  parent_touch_source.set_error_handler([](zx_status_t status) {
    FAIL("Touch source closed with status: %s", zx_status_get_string(status));
  });

  // Create the parent view and attach it to |root_session_|. Register the parent view to receive
  // input events.
  {
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    parent_session = realm_->Connect<fuchsia::ui::composition::Flatland>();
    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    ViewBoundProtocols protocols;
    protocols.set_touch_source(parent_touch_source.NewRequest());
    auto identity = scenic::NewViewIdentityOnCreation();
    auto parent_view_ref = fidl::Clone(identity.view_ref);

    TransformId kTransformId = {.value = 2};
    ConnectChildView(
        root_session_, std::move(parent_token),
        {static_cast<uint32_t>(display_width_), static_cast<uint32_t>(display_height_)},
        kTransformId, kRootContentId);

    parent_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                                parent_viewport_watcher.NewRequest());

    parent_session->CreateTransform(kRootTransform);
    parent_session->SetRootTransform(kRootTransform);

    // The parent's Present call generates a snapshot which includes the ViewRef.
    BlockingPresent(parent_session);
    RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(parent_view_ref),
                     DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);
  }
  auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
  TransformId kTransformId = {.value = 2};
  ContentId kContent = {.value = 2};

  // Create child view.
  fuchsia::ui::composition::FlatlandPtr child_session;
  fuchsia::ui::pointer::TouchSourcePtr child_touch_source;
  child_session = realm_->Connect<fuchsia::ui::composition::Flatland>();
  child_touch_source.set_error_handler([](zx_status_t status) {
    FAIL("Touch source A closed with status: %s", zx_status_get_string(status));
  });

  ConnectChildView(parent_session, std::move(parent_token), FullscreenSize(), kTransformId,
                   kContent);

  // Set up child view.
  {
    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    auto child_view_ref = fidl::Clone(identity.view_ref);
    fuchsia::ui::composition::ViewBoundProtocols protocols;
    protocols.set_touch_source(child_touch_source.NewRequest());
    child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                               parent_viewport_watcher.NewRequest());
    child_session->CreateTransform(kRootTransform);
    child_session->SetRootTransform(kRootTransform);
  }

  // Commit all changes.
  BlockingPresent(root_session_);
  BlockingPresent(parent_session);
  BlockingPresent(child_session);

  // Listen for input events.
  std::vector<TouchEvent> parent_events;
  StartWatchLoop(parent_touch_source, parent_events);
  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Begin injection - both child and parent should receive it.
  Inject(0, 0, fupi_EventPhase::ADD);
  Inject(1, 1, fupi_EventPhase::CHANGE);

  // Succeeds or times out.
  RunLoopUntil([&child_events, &parent_events] {
    return child_events.size() == 2u && parent_events.size() == 2u;
  });

  // Disconnect |child_session| and observe that it gets a cancellation event, while
  // |parent_session| keeps receiving events and receives a GRANTED interaction result.
  parent_session->RemoveChild(kRootTransform, kTransformId);
  BlockingPresent(parent_session);

  Inject(2, 2, fupi_EventPhase::CHANGE);
  Inject(3, 3, fupi_EventPhase::CHANGE);

  // Succeeds or times out.
  RunLoopUntil([&child_events, &parent_events] {
    return child_events.size() == 3u && parent_events.size() == 5u;
  });

  ASSERT_TRUE(child_events.back().has_interaction_result());
  EXPECT_EQ(child_events.back().interaction_result().status, TouchInteractionStatus::DENIED);

  EXPECT_TRUE(std::any_of(parent_events.begin(), parent_events.end(), [](const TouchEvent& event) {
    return event.has_interaction_result() &&
           event.interaction_result().status == TouchInteractionStatus::GRANTED;
  }));
}

// Construct a scene with the following topology:
//
// Root
//   |
// Parent
//   |
// Child
//
// Injects in HitTest mode, all events delivered to Parent and Child. Parent replies "NO" to its
// events, so Child wins the contest. Then, disconnect child disconnect Child and observe cancel
// event delivered to Child.
TEST_F(FlatlandTouchIntegrationTest, HitTested_ViewDisconnectedAfterWinning_ShouldCancelStream) {
  fuchsia::ui::composition::FlatlandPtr parent_session;
  fuchsia::ui::pointer::TouchSourcePtr parent_touch_source;
  parent_touch_source.set_error_handler([](zx_status_t status) {
    FAIL("Touch source closed with status: %s", zx_status_get_string(status));
  });

  // Create the parent view and attach it to |root_session_|. Register the parent view to receive
  // input events.
  {
    auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
    parent_session = realm_->Connect<fuchsia::ui::composition::Flatland>();
    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    ViewBoundProtocols protocols;
    protocols.set_touch_source(parent_touch_source.NewRequest());
    auto identity = scenic::NewViewIdentityOnCreation();
    auto parent_view_ref = fidl::Clone(identity.view_ref);

    TransformId kTransformId = {.value = 2};
    ConnectChildView(
        root_session_, std::move(parent_token),
        {static_cast<uint32_t>(display_width_), static_cast<uint32_t>(display_height_)},
        kTransformId, kRootContentId);

    parent_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                                parent_viewport_watcher.NewRequest());

    parent_session->CreateTransform(kRootTransform);
    parent_session->SetRootTransform(kRootTransform);

    // The parent's Present call generates a snapshot which includes the ViewRef.
    BlockingPresent(parent_session);
    RegisterInjector(fidl::Clone(root_view_ref_), fidl::Clone(parent_view_ref),
                     DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);
  }
  auto [child_token, parent_token] = scenic::ViewCreationTokenPair::New();
  TransformId kTransformId = {.value = 2};
  ContentId kContent = {.value = 2};

  // Create child view A.
  fuchsia::ui::composition::FlatlandPtr child_session;
  fuchsia::ui::pointer::TouchSourcePtr child_touch_source;
  child_session = realm_->Connect<fuchsia::ui::composition::Flatland>();
  child_touch_source.set_error_handler([](zx_status_t status) {
    FAIL("Touch source A closed with status: %s", zx_status_get_string(status));
  });

  ConnectChildView(parent_session, std::move(parent_token), FullscreenSize(), kTransformId,
                   kContent);

  // Set up child view A.
  {
    fidl::InterfacePtr<ParentViewportWatcher> parent_viewport_watcher;
    auto identity = scenic::NewViewIdentityOnCreation();
    auto child_view_ref = fidl::Clone(identity.view_ref);
    fuchsia::ui::composition::ViewBoundProtocols protocols;
    protocols.set_touch_source(child_touch_source.NewRequest());
    child_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                               parent_viewport_watcher.NewRequest());
    child_session->CreateTransform(kRootTransform);
    child_session->SetRootTransform(kRootTransform);
  }

  // Commit all changes.
  BlockingPresent(root_session_);
  BlockingPresent(parent_session);
  BlockingPresent(child_session);

  // Listen for input events.
  std::vector<TouchEvent> parent_events;
  StartWatchLoop(parent_touch_source, parent_events, TouchResponseType::NO);
  std::vector<TouchEvent> child_events;
  StartWatchLoop(child_touch_source, child_events);

  // Begin injection.
  Inject(0, 0, fupi_EventPhase::ADD);
  Inject(5, 0, fupi_EventPhase::CHANGE);

  // Child should win the contest.
  RunLoopUntil([&child_events] { return child_events.size() == 3u; });  // Succeeds or times out.
  ASSERT_EQ(child_events.size(), 3u);
  EXPECT_TRUE(std::any_of(child_events.begin(), child_events.end(), [](const TouchEvent& event) {
    return event.has_interaction_result() &&
           event.interaction_result().status == TouchInteractionStatus::GRANTED;
  }));

  // Detach child_session from the scene graph.
  parent_session->RemoveChild(kRootTransform, kTransformId);
  BlockingPresent(parent_session);

  // Next event should deliver CANCEL to Child.
  Inject(5, 5, fupi_EventPhase::CHANGE);
  RunLoopUntil([&child_events] { return child_events.size() == 4u; });  // Succeeds or times out.
  ASSERT_EQ(child_events.size(), 4u);
  ASSERT_TRUE(child_events.back().has_pointer_sample());
  ASSERT_TRUE(child_events.back().pointer_sample().has_phase());
  EXPECT_EQ(child_events.back().pointer_sample().phase(), EventPhase::CANCEL);

  // Future injections should be ignored.
  parent_events.clear();
  child_events.clear();
  Inject(0, 5, fupi_EventPhase::CHANGE);
  EXPECT_TRUE(parent_events.empty());
  EXPECT_TRUE(child_events.empty());
}

}  // namespace integration_tests
