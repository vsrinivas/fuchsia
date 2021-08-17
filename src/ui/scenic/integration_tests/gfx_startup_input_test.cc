// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/lifecycle/cpp/fidl.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/ui/scenic/integration_tests/utils.h"

// These tests exercise InputSystem logic during startup, e.g. potential race conditions.

namespace integration_tests {

const std::map<std::string, std::string> LocalServices() {
  return {{"fuchsia.ui.composition.Allocator",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.scenic.Scenic",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
          {"fuchsia.ui.lifecycle.LifecycleController",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          {"fuchsia.hardware.display.Provider",
           "fuchsia-pkg://fuchsia.com/fake-hardware-display-controller-provider#meta/hdcp.cmx"}};
}

// Allow these global services.
const std::vector<std::string> GlobalServices() {
  return {"fuchsia.vulkan.loader.Loader", "fuchsia.sysmem.Allocator"};
}

scenic::Session CreateSession(fuchsia::ui::scenic::Scenic* scenic,
                              fuchsia::ui::scenic::SessionEndpoints endpoints) {
  FX_DCHECK(scenic);
  FX_DCHECK(!endpoints.has_session());
  FX_DCHECK(!endpoints.has_session_listener());

  fuchsia::ui::scenic::SessionPtr session_ptr;
  fuchsia::ui::scenic::SessionListenerHandle listener_handle;
  auto listener_request = listener_handle.NewRequest();

  endpoints.set_session(session_ptr.NewRequest());
  endpoints.set_session_listener(std::move(listener_handle));
  scenic->CreateSessionT(std::move(endpoints), [] {});

  return scenic::Session(std::move(session_ptr), std::move(listener_request));
}

// Test fixture that sets up an environment with a Scenic we can connect to.
class GfxStartupInputTest : public gtest::TestWithEnvironmentFixture {
 protected:
  fuchsia::ui::scenic::Scenic* scenic() { return scenic_.get(); }

  void SetUp() override {
    TestWithEnvironmentFixture::SetUp();

    environment_ = CreateNewEnclosingEnvironment(
        "gfx_view_ref_installed_integration_test_environment", CreateServices());
    WaitForEnclosingEnvToStart(environment_.get());

    // Connects to scenic lifecycle controller in order to shutdown scenic at the end of the test.
    // This ensures the correct ordering of shutdown under CFv1: first scenic, then the fake display
    // controller.
    //
    // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
    environment_->ConnectToService<fuchsia::ui::lifecycle::LifecycleController>(
        scenic_lifecycle_controller_.NewRequest());

    environment_->ConnectToService(scenic_.NewRequest());
    scenic_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
    });
  }

  void TearDown() override {
    // Avoid spurious errors since we are about to kill scenic.
    //
    // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
    scenic_.set_error_handler(nullptr);

    zx_status_t terminate_status = scenic_lifecycle_controller_->Terminate();
    FX_CHECK(terminate_status == ZX_OK)
        << "Failed to terminate Scenic with status: " << zx_status_get_string(terminate_status);
  }

  void BlockingPresent(scenic::Session& session) {
    bool presented = false;
    session.set_on_frame_presented_handler([&presented](auto) { presented = true; });
    session.Present2(0, 0, [](auto) {});
    RunLoopUntil([&presented] { return presented; });
    session.set_on_frame_presented_handler([](auto) {});
  }

  // Injects an arbitrary input event using the legacy injection API.
  // Uses a new pointer on each injection to minimize conteracting between different injections.
  void InjectFreshEvent(scenic::Session& session, uint32_t compositor_id) {
    PointerCommandGenerator pointer(compositor_id, /*device id*/ 1,
                                    /*pointer id*/ ++last_pointer_id_,
                                    fuchsia::ui::input::PointerEventType::TOUCH);
    session.Enqueue(pointer.Add(2.5, 2.5));
    BlockingPresent(session);
  }

  // Configures services available to the test environment. This method is called by |SetUp()|. It
  // shadows but calls |TestWithEnvironmentFixture::CreateServices()|.
  std::unique_ptr<sys::testing::EnvironmentServices> CreateServices() {
    auto services = TestWithEnvironmentFixture::CreateServices();
    for (const auto& [name, url] : LocalServices()) {
      const zx_status_t is_ok = services->AddServiceWithLaunchInfo({.url = url}, name);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << name;
    }

    for (const auto& service : GlobalServices()) {
      const zx_status_t is_ok = services->AllowParentService(service);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << service;
    }

    return services;
  }

 private:
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  fuchsia::ui::lifecycle::LifecycleControllerSyncPtr scenic_lifecycle_controller_;
  fuchsia::ui::scenic::ScenicPtr scenic_;

  uint32_t last_pointer_id_ = 0;
};

// This test builds up a scene piece by piece, injecting input at every point to confirm
// that there is no crash.
TEST_F(GfxStartupInputTest, LegacyInjectBeforeSceneSetupComplete_ShouldNotCrash) {
  constexpr uint32_t kFakeCompositorId = 321241;
  scenic::Session session = CreateSession(scenic(), {});
  std::vector<fuchsia::ui::input::InputEvent> received_input_events;
  session.set_event_handler([&received_input_events](auto events) {
    for (auto& event : events) {
      if (event.is_input() && !event.input().is_focus())
        received_input_events.emplace_back(std::move(event.input()));
    }
  });

  // Set up a view to receive input in.
  auto [v, vh] = scenic::ViewTokenPair::New();
  scenic::ViewHolder holder(&session, std::move(vh), "holder");
  holder.SetViewProperties({.bounding_box = {.max = {5, 5, 1}}});
  scenic::View view(&session, std::move(v), "view");
  scenic::ShapeNode shape(&session);
  scenic::Rectangle rec(&session, 5, 5);
  shape.SetShape(rec);
  shape.SetTranslation(2.5f, 2.5f, 0);  // Center the shape within the View.
  view.AddChild(shape);

  // Empty.
  BlockingPresent(session);
  InjectFreshEvent(session, kFakeCompositorId);
  EXPECT_TRUE(received_input_events.empty());

  // Only a Scene object.
  scenic::Scene scene(&session);
  BlockingPresent(session);
  InjectFreshEvent(session, kFakeCompositorId);
  EXPECT_TRUE(received_input_events.empty());

  // Attach the view to the scene now that we have a scene.
  scene.AddChild(holder);

  scenic::Camera camera(scene);
  BlockingPresent(session);
  InjectFreshEvent(session, kFakeCompositorId);
  EXPECT_TRUE(received_input_events.empty());

  scenic::Renderer renderer(&session);
  BlockingPresent(session);
  InjectFreshEvent(session, kFakeCompositorId);
  EXPECT_TRUE(received_input_events.empty());

  renderer.SetCamera(camera);
  BlockingPresent(session);
  InjectFreshEvent(session, kFakeCompositorId);
  EXPECT_TRUE(received_input_events.empty());

  scenic::Compositor compositor(&session);
  BlockingPresent(session);
  const uint32_t compositor_id = compositor.id();
  InjectFreshEvent(session, kFakeCompositorId);  // With fake compositor id.
  InjectFreshEvent(session, compositor_id);      // With real compositor id.

  scenic::LayerStack layer_stack(&session);
  BlockingPresent(session);
  InjectFreshEvent(session, compositor_id);
  EXPECT_TRUE(received_input_events.empty());

  compositor.SetLayerStack(layer_stack);
  BlockingPresent(session);
  InjectFreshEvent(session, compositor_id);
  EXPECT_TRUE(received_input_events.empty());

  scenic::Layer layer(&session);
  BlockingPresent(session);
  InjectFreshEvent(session, compositor_id);
  EXPECT_TRUE(received_input_events.empty());

  layer_stack.AddLayer(layer);
  BlockingPresent(session);
  InjectFreshEvent(session, compositor_id);
  EXPECT_TRUE(received_input_events.empty());

  layer.SetRenderer(renderer);
  BlockingPresent(session);
  InjectFreshEvent(session, compositor_id);
  EXPECT_TRUE(received_input_events.empty());

  layer.SetSize(10, 10);
  BlockingPresent(session);
  InjectFreshEvent(session, compositor_id);

  // Should now have received the final event.
  EXPECT_FALSE(received_input_events.empty());
}

}  // namespace integration_tests
