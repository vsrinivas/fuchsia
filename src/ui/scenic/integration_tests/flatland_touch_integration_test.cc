// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/pointer/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <lib/async-loop/testing/cpp/real_loop.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <zircon/status.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <sdk/lib/ui/scenic/cpp/view_creation_tokens.h>
#include <zxtest/zxtest.h>

#include "src/ui/scenic/integration_tests/scenic_realm_builder.h"
#include "src/ui/scenic/integration_tests/utils.h"

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
using fupi_EventPhase = fuchsia::ui::pointerinjector::EventPhase;
using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::ContentId;
using fuchsia::ui::composition::Flatland;
using fuchsia::ui::composition::FlatlandDisplay;
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
using Mat3 = std::array<std::array<float, 3>, 3>;
using Vec3 = std::array<float, 3>;
using RealmRoot = sys::testing::experimental::RealmRoot;

namespace {

Mat3 ArrayToMat3(std::array<float, 9> array) {
  Mat3 mat;
  for (size_t row = 0; row < mat.size(); row++) {
    for (size_t col = 0; col < mat[0].size(); col++) {
      mat[row][col] = array[mat.size() * row + col];
    }
  }
  return mat;
}

// Matrix multiplication between a 3X3 matrix and 3X1 matrix.
Vec3 operator*(const Mat3& mat, const Vec3& vec) {
  Vec3 result = {0, 0, 0};
  for (size_t row = 0; row < mat.size(); row++) {
    for (size_t col = 0; col < mat[0].size(); col++) {
      result[row] += mat[row][col] * vec[col];
    }
  }
  return result;
}

Vec3& operator/(Vec3& vec, float num) {
  for (size_t i = 0; i < vec.size(); i++) {
    vec[i] /= num;
  }
  return vec;
}

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
    realm_ = ScenicRealmBuilder(
                 "fuchsia-pkg://fuchsia.com/flatland_integration_tests#meta/scenic_subrealm.cm")
                 .AddScenicSubRealmProtocol(fuchsia::ui::composition::Flatland::Name_)
                 .AddScenicSubRealmProtocol(fuchsia::ui::composition::FlatlandDisplay::Name_)
                 .AddScenicSubRealmProtocol(fuchsia::ui::composition::Allocator::Name_)
                 .AddScenicSubRealmProtocol(fuchsia::ui::pointerinjector::Registry::Name_)
                 .Build();

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
    injector_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
    });
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
                        ViewportCreationToken&& token) {
    // Let the client_end die.
    fidl::InterfacePtr<ChildViewWatcher> child_view_watcher;
    ViewportProperties properties;
    properties.set_logical_size({kDefaultSize, kDefaultSize});

    const TransformId kTransform{.value = 1};
    flatland->CreateTransform(kTransform);
    flatland->SetRootTransform(kTransform);

    const ContentId kContent{.value = 1};
    flatland->CreateViewport(kContent, std::move(token), std::move(properties),
                             child_view_watcher.NewRequest());
    flatland->SetContent(kTransform, kContent);

    BlockingPresent(flatland);
  }

  const uint32_t kDefaultSize = 1;
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
  ConnectChildView(root_session_, std::move(parent_token));

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
  Inject(0, 0, fupi_EventPhase::ADD);
  Inject(display_width_, 0, fupi_EventPhase::CHANGE);
  Inject(display_width_, display_height_, fupi_EventPhase::CHANGE);
  Inject(0, display_height_, fupi_EventPhase::REMOVE);
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

// Creates a view tree of the form
// root_view
//    |
// parent_view
//    |
// child_view
// The parent's view gets created using CreateView2 but the child's view gets created using
// CreateView. As a result, the child  will not receive any input events since it does not have an
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

    ConnectChildView(root_session_, std::move(parent_token));

    parent_session->CreateView2(std::move(child_token), std::move(identity), std::move(protocols),
                                parent_viewport_watcher.NewRequest());

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

    ConnectChildView(parent_session, std::move(parent_token));

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
}  // namespace integration_tests
