// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/pointer/augment/cpp/fidl.h>
#include <fuchsia/ui/pointer/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <lib/async-loop/testing/cpp/real_loop.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <memory>
#include <vector>

#include <zxtest/zxtest.h>

#include "src/ui/scenic/tests/utils/scenic_realm_builder.h"
#include "src/ui/scenic/tests/utils/utils.h"

// These tests exercise the integration between GFX and the InputSystem for TouchSourceWithLocalHit.
// Setup:
// - Injection done in context View Space, with fuchsia.ui.pointerinjector
// - Target(s) specified by View (using view ref koids)
// - Dispatch done to fuchsia.ui.pointer.TouchSourceWithLocalHit in receiver(s') View Space.

namespace integration_tests {

using fuchsia::ui::pointer::TouchInteractionStatus;
using fupi_EventPhase = fuchsia::ui::pointerinjector::EventPhase;
using fuchsia::ui::pointer::EventPhase;
using fuchsia::ui::pointer::TouchEvent;
using fuchsia::ui::pointer::TouchResponse;
using fuchsia::ui::pointer::TouchResponseType;
using fuchsia::ui::pointer::augment::TouchEventWithLocalHit;

namespace {

struct SessionWithTouchSource {
  std::unique_ptr<scenic::Session> session;
  fuchsia::ui::pointer::TouchSourcePtr touch_source_ptr;
};

SessionWithTouchSource CreateSessionWithTouchSource(fuchsia::ui::scenic::Scenic* scenic) {
  SessionWithTouchSource session_with_touch_source;

  fuchsia::ui::scenic::SessionEndpoints endpoints;
  fuchsia::ui::scenic::SessionPtr session_ptr;
  fuchsia::ui::scenic::SessionListenerHandle listener_handle;
  auto listener_request = listener_handle.NewRequest();
  endpoints.set_session(session_ptr.NewRequest());
  endpoints.set_session_listener(std::move(listener_handle));
  endpoints.set_touch_source(session_with_touch_source.touch_source_ptr.NewRequest());
  scenic->CreateSessionT(std::move(endpoints), [] {});

  session_with_touch_source.session =
      std::make_unique<scenic::Session>(std::move(session_ptr), std::move(listener_request));
  return session_with_touch_source;
}

// Sets up the root of a scene.
// Present() must be called separately by the creator, since this does not have access to the
// looper.
struct RootSession {
  RootSession(fuchsia::ui::scenic::Scenic* scenic)
      : session_with_touch_source(CreateSessionWithTouchSource(scenic)),
        session(session_with_touch_source.session.get()),
        compositor(session),
        layer_stack(session),
        layer(session),
        renderer(session),
        scene(session),
        camera(scene),
        root_view_token_pair(scenic::ViewTokenPair::New()),
        view_holder(session, std::move(root_view_token_pair.view_holder_token), "root_view_holder"),
        root_view_ref_pair(scenic::ViewRefPair::New()),
        view(session, std::move(root_view_token_pair.view_token),
             std::move(root_view_ref_pair.control_ref), fidl::Clone(root_view_ref_pair.view_ref),
             "root_view"),
        child_view_token_pair(scenic::ViewTokenPair::New()),
        child_view_holder(session, std::move(child_view_token_pair.view_holder_token),
                          "child_view_holder") {
    static constexpr fuchsia::ui::gfx::ViewProperties k8x8x8 = {
        .bounding_box = {.min = {0, 0, -8}, .max = {8, 8, 0}}};
    compositor.SetLayerStack(layer_stack);
    layer_stack.AddLayer(layer);
    layer.SetSize(/*width*/ 8, /*height*/ 8);  // 8x8 "display".
    layer.SetRenderer(renderer);
    renderer.SetCamera(camera);
    scene.AddChild(view_holder);
    view_holder.SetViewProperties(k8x8x8);
    view.AddChild(child_view_holder);
    child_view_holder.SetViewProperties(k8x8x8);
  }

  SessionWithTouchSource session_with_touch_source;
  scenic::Session* session;
  scenic::DisplayCompositor compositor;
  scenic::LayerStack layer_stack;
  scenic::Layer layer;
  scenic::Renderer renderer;
  scenic::Scene scene;
  scenic::Camera camera;
  scenic::ViewTokenPair root_view_token_pair;
  scenic::ViewHolder view_holder;
  scenic::ViewRefPair root_view_ref_pair;
  scenic::View view;
  scenic::ViewTokenPair child_view_token_pair;
  scenic::ViewHolder child_view_holder;
};

}  // namespace

class GfxTouchLocalHitIntegrationTest : public zxtest::Test, public loop_fixture::RealLoop {
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

  fuchsia::ui::scenic::Scenic* scenic() { return scenic_.get(); }

  void SetUp() override {
    // Build the realm topology and route the protocols required by this test fixture from the
    // scenic subrealm.
    realm_ = std::make_unique<component_testing::RealmRoot>(
        ScenicRealmBuilder()
            .AddRealmProtocol(fuchsia::ui::scenic::Scenic::Name_)
            .AddRealmProtocol(fuchsia::ui::pointer::augment::LocalHit::Name_)
            .AddRealmProtocol(fuchsia::ui::pointerinjector::Registry::Name_)
            .Build());

    scenic_ = realm_->Connect<fuchsia::ui::scenic::Scenic>();
    scenic_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to Scenic: %s", zx_status_get_string(status));
    });
    registry_ = realm_->Connect<fuchsia::ui::pointerinjector::Registry>();
    registry_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to pointerinjector Registry: %s", zx_status_get_string(status));
    });

    local_hit_registry_ = realm_->Connect<fuchsia::ui::pointer::augment::LocalHit>();
    local_hit_registry_.set_error_handler([](zx_status_t status) {
      FAIL("Lost connection to LocalHit Registry: %s", zx_status_get_string(status));
    });

    // Set up root view.
    root_session_ = std::make_unique<RootSession>(scenic());
    root_session_->session->set_error_handler([](auto) { FAIL("Root session terminated."); });
    BlockingPresent(*root_session_->session);
  }

  void BlockingPresent(scenic::Session& session) {
    bool presented = false;
    session.set_on_frame_presented_handler([&presented](auto) { presented = true; });
    session.Present2(0, 0, [](auto) {});
    RunLoopUntil([&presented] { return presented; });
    session.set_on_frame_presented_handler([](auto) {});
  }

  std::tuple<std::unique_ptr<scenic::Session>, fuchsia::ui::pointer::TouchSourcePtr, scenic::View>
  CreateChildView(fuchsia::ui::views::ViewToken view_token,
                  fuchsia::ui::views::ViewRefControl control_ref,
                  fuchsia::ui::views::ViewRef view_ref, std::string debug_name, float width,
                  float height) {
    auto [session, touch_source] = CreateSessionWithTouchSource(scenic());
    scenic::View view(session.get(), std::move(view_token), std::move(control_ref),
                      std::move(view_ref), debug_name);
    scenic::ShapeNode shape(session.get());
    scenic::Rectangle rec(session.get(), width, height);
    shape.SetTranslation(width / 2.f, height / 2.f, 0.f);  // Center the shape within the View.
    view.AddChild(shape);
    shape.SetShape(rec);
    BlockingPresent(*session);

    return {std::move(session), std::move(touch_source), std::move(view)};
  }

  void Inject(float x, float y, fupi_EventPhase phase) {
    FX_CHECK(injector_);
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
                        fuchsia::ui::views::ViewRef target_view_ref) {
    fuchsia::ui::pointerinjector::Config config;
    config.set_device_id(kDeviceId);
    config.set_device_type(fuchsia::ui::pointerinjector::DeviceType::TOUCH);
    config.set_dispatch_policy(
        fuchsia::ui::pointerinjector::DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET);
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
        viewport.set_viewport_to_context_transform(kIdentityMatrix);
        config.set_viewport(std::move(viewport));
      }
    }

    injector_.set_error_handler([this](zx_status_t) { injector_channel_closed_ = true; });
    bool register_callback_fired = false;
    registry_->Register(std::move(config), injector_.NewRequest(),
                        [&register_callback_fired] { register_callback_fired = true; });
    RunLoopUntil([&register_callback_fired] { return register_callback_fired; });
    ASSERT_FALSE(injector_channel_closed_);
  }

  // Starts a recursive TouchSource::Watch() loop that collects all received events into
  // |out_events|.
  void StartWatchLoop(fuchsia::ui::pointer::augment::TouchSourceWithLocalHitPtr& touch_source,
                      std::vector<TouchEventWithLocalHit>& out_events,
                      TouchResponseType response_type = TouchResponseType::MAYBE) {
    const size_t index = watch_loops_.size();
    watch_loops_.emplace_back();
    watch_loops_.at(index) = [this, &touch_source, &out_events, response_type, index](auto events) {
      std::vector<TouchResponse> responses;
      for (auto& event : events) {
        if (event.touch_event.has_pointer_sample()) {
          TouchResponse response;
          response.set_response_type(response_type);
          responses.emplace_back(std::move(response));
        } else {
          responses.emplace_back();
        }
      }
      std::move(events.begin(), events.end(), std::back_inserter(out_events));

      touch_source->Watch(std::move(responses), [this, index](auto events) {
        watch_loops_.at(index)(std::move(events));
      });
    };
    touch_source->Watch({}, watch_loops_.at(index));
  }

  std::array<std::array<float, 2>, 2> FullScreenExtents() const { return {{{0, 0}, {8, 8}}}; }

  std::unique_ptr<RootSession> root_session_;
  bool injector_channel_closed_ = false;
  fuchsia::ui::pointer::augment::LocalHitPtr local_hit_registry_;

 private:
  fuchsia::ui::scenic::ScenicPtr scenic_;
  fuchsia::ui::pointerinjector::RegistryPtr registry_;
  fuchsia::ui::pointerinjector::DevicePtr injector_;

  // Holds watch loops so they stay alive through the duration of the test.
  std::vector<std::function<void(std::vector<TouchEventWithLocalHit>)>> watch_loops_;
  std::unique_ptr<component_testing::RealmRoot> realm_;
};

// In this test we set up three views beneath the root:
// 1 and it's two children: 2 and 3. Each view has a rectangle covering their entire View.
// View 3 is above View 2, which is above View 1.
// We then drag the pointer diagonally across all views and observe that the expected local
// hits are delivered.
//
// 1: View 1, 2: View 2, 3: View 3, x: No view, []: touch point
//
//   X ->
// Y [1] 1  1  1  1  1  1  x
// |  1 [2] 2  2  1  1  1  x
// v  1  2 [2] 2  1  1  1  x
//    1  2  2 [3] 3  3  1  x
//    1  1  1  3 [3] 3  1  x
//    1  1  1  3  3 [3] 1  x
//    1  1  1  1  1  1 [1] x
//    x  x  x  x  x  x  x [x]
//
TEST_F(GfxTouchLocalHitIntegrationTest, InjectedInput_ShouldBeCorrectlyTransformed) {
  static constexpr fuchsia::ui::gfx::ViewProperties k3x3x1 = {.bounding_box = {.max = {3, 3, 1}}};

  // Create View 1
  auto [control_ref, view_ref] = scenic::ViewRefPair::New();
  const zx_koid_t view1_koid = ExtractKoid(view_ref);
  auto [session, touch_source, view1] =
      CreateChildView(std::move(root_session_->child_view_token_pair.view_token),
                      std::move(control_ref), fidl::Clone(view_ref), "view1", 7.f, 7.f);

  // Create View 2
  auto [view2_control_ref, view2_ref] = scenic::ViewRefPair::New();
  const zx_koid_t view2_koid = ExtractKoid(view2_ref);
  auto [view2_vt, view2_vht] = scenic::ViewTokenPair::New();
  auto [view2_session, _view2_ts, _view2] = CreateChildView(
      std::move(view2_vt), std::move(view2_control_ref), std::move(view2_ref), "view2", 3.f, 3.f);

  // Attach View 2
  scenic::ViewHolder view2_holder(session.get(), std::move(view2_vht), "view2_holder");
  view2_holder.SetViewProperties(k3x3x1);
  view2_holder.SetTranslation(1.f, 1.f, -1.f);
  view1.AddChild(view2_holder);

  // Create View 3
  auto [view3_control_ref, view3_ref] = scenic::ViewRefPair::New();
  const zx_koid_t view3_koid = ExtractKoid(view3_ref);
  auto [view3_vt, view3_vht] = scenic::ViewTokenPair::New();
  auto [view3_session, _view3_ts, _view3] = CreateChildView(
      std::move(view3_vt), std::move(view3_control_ref), std::move(view3_ref), "view3", 3.f, 3.f);

  // Attach View 3
  scenic::ViewHolder view3_holder(session.get(), std::move(view3_vht), "view3_holder");
  view3_holder.SetViewProperties(k3x3x1);
  view3_holder.SetTranslation(3.f, 3.f, -2.f);
  view1.AddChild(view3_holder);

  BlockingPresent(*session);

  // Upgrade View 1's touch source
  fuchsia::ui::pointer::augment::TouchSourceWithLocalHitPtr touch_source_with_local_hit = nullptr;
  local_hit_registry_->Upgrade(std::move(touch_source),
                               [&touch_source_with_local_hit](auto upgraded, auto error) {
                                 EXPECT_TRUE(upgraded);
                                 ASSERT_FALSE(error);
                                 touch_source_with_local_hit = upgraded.Bind();
                               });
  RunLoopUntil([&touch_source_with_local_hit] { return touch_source_with_local_hit.is_bound(); });

  std::vector<TouchEventWithLocalHit> child_events;
  StartWatchLoop(touch_source_with_local_hit, child_events);

  // Begin test.
  RegisterInjector(fidl::Clone(root_session_->root_view_ref_pair.view_ref), std::move(view_ref));
  Inject(0.5f, 0.5f, fupi_EventPhase::ADD);
  Inject(1.5f, 1.5f, fupi_EventPhase::CHANGE);
  Inject(2.5f, 2.5f, fupi_EventPhase::CHANGE);
  Inject(3.5f, 3.5f, fupi_EventPhase::CHANGE);
  Inject(4.5f, 4.5f, fupi_EventPhase::CHANGE);
  Inject(5.5f, 5.5f, fupi_EventPhase::CHANGE);
  Inject(6.5f, 6.5f, fupi_EventPhase::CHANGE);
  Inject(7.5f, 7.5f, fupi_EventPhase::REMOVE);
  RunLoopUntil([&child_events] { return child_events.size() == 8u; });  // Succeeds or times out.

  EXPECT_EQ(child_events.at(0).local_viewref_koid, view1_koid);
  EXPECT_EQ(child_events.at(1).local_viewref_koid, view2_koid);
  EXPECT_EQ(child_events.at(2).local_viewref_koid, view2_koid);
  EXPECT_EQ(child_events.at(3).local_viewref_koid, view3_koid);
  EXPECT_EQ(child_events.at(4).local_viewref_koid, view3_koid);
  EXPECT_EQ(child_events.at(5).local_viewref_koid, view3_koid);
  EXPECT_EQ(child_events.at(6).local_viewref_koid, view1_koid);
  EXPECT_EQ(child_events.at(7).local_viewref_koid, ZX_KOID_INVALID);  // No View

  EXPECT_EQ(child_events.at(0).local_point[0], 0.5f);  // View 1
  EXPECT_EQ(child_events.at(1).local_point[0], 0.5f);  // View 2
  EXPECT_EQ(child_events.at(2).local_point[0], 1.5f);  // View 2
  EXPECT_EQ(child_events.at(3).local_point[0], 0.5f);  // View 3
  EXPECT_EQ(child_events.at(4).local_point[0], 1.5f);  // View 3
  EXPECT_EQ(child_events.at(5).local_point[0], 2.5f);  // View 3
  EXPECT_EQ(child_events.at(6).local_point[0], 6.5f);  // View 1
  EXPECT_EQ(child_events.at(7).local_point[0], 0.0f);  // No View
}

}  // namespace integration_tests
