// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/lifecycle/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

// This test exercises the fuchsia.ui.views.ViewRefInstalled protocol implemented by Scenic
// in the context of the GFX compositor interface.
// The geometry is not important in this test, so we use the following minimal two-node
// (plus a scene node) tree topology:
//   (scene)
//      |
//    parent
//      |
//    child
namespace integration_tests {

const std::map<std::string, std::string> LocalServices() {
  return {{"fuchsia.ui.composition.Allocator",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.scenic.Scenic",
           "fuchsia-pkg://fuchsia.com/gfx_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.views.ViewRefInstalled",
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

// "Long enough" time to wait before assuming a FIDL message won't arrive.
// Should not be used when actually expecting an update to occur, to avoid flakiness.
const zx::duration kWaitTime = zx::msec(2);

using fuchsia::ui::views::ViewRef;
using WatchResult = fuchsia::ui::views::ViewRefInstalled_Watch_Result;

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

// Sets up the root of a scene.
// Present() must be called separately by the creator, since this does not have access to the
// looper.
struct RootSession {
  RootSession(fuchsia::ui::scenic::Scenic* scenic, fuchsia::ui::scenic::SessionEndpoints endpoints)
      : session(CreateSession(scenic, std::move(endpoints))),
        compositor(&session),
        layer_stack(&session),
        layer(&session),
        renderer(&session),
        scene(&session),
        camera(scene) {
    compositor.SetLayerStack(layer_stack);
    layer_stack.AddLayer(layer);
    layer.SetRenderer(renderer);
    renderer.SetCamera(camera);
  }

  scenic::Session session;
  scenic::DisplayCompositor compositor;
  scenic::LayerStack layer_stack;
  scenic::Layer layer;
  scenic::Renderer renderer;
  scenic::Scene scene;
  scenic::Camera camera;

  std::unique_ptr<scenic::ViewHolder> view_holder;
};

// Test fixture that sets up an environment with a Scenic we can connect to.
class GfxViewRefInstalledIntegrationTest : public gtest::TestWithEnvironmentFixture {
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

    environment_->ConnectToService(view_ref_installed_ptr_.NewRequest());
    view_ref_installed_ptr_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to ViewRefInstalled: " << zx_status_get_string(status);
    });

    // Set up root view.
    root_session_ =
        std::make_unique<RootSession>(scenic(), fuchsia::ui::scenic::SessionEndpoints{});
    root_session_->session.set_error_handler([](auto) { FAIL() << "Root session terminated."; });
    BlockingPresent(root_session_->session);
  }

  void TearDown() override {
    // Avoid spurious errors since we are about to kill scenic.
    //
    // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
    view_ref_installed_ptr_.set_error_handler(nullptr);
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

  void AttachToScene(fuchsia::ui::views::ViewHolderToken token) {
    root_session_->view_holder =
        std::make_unique<scenic::ViewHolder>(&root_session_->session, std::move(token), "holder");
    root_session_->scene.AddChild(*root_session_->view_holder);
    BlockingPresent(root_session_->session);
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

  std::unique_ptr<RootSession> root_session_;
  fuchsia::ui::views::ViewRefInstalledPtr view_ref_installed_ptr_;

 private:
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  fuchsia::ui::lifecycle::LifecycleControllerSyncPtr scenic_lifecycle_controller_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
};

TEST_F(GfxViewRefInstalledIntegrationTest, InvalidatedViewRef_ShouldReturnError) {
  std::optional<WatchResult> result;
  {
    auto [control_ref, view_ref] = scenic::ViewRefPair::New();
    view_ref_installed_ptr_->Watch(std::move(view_ref), [&result](auto watch_result) {
      result.emplace(std::move(watch_result));
    });
    RunLoopWithTimeout(kWaitTime);
    EXPECT_FALSE(result.has_value());
  }  // |control_ref| goes out of scope. This will invalidate the ViewRef.

  RunLoopUntil([&result] { return result.has_value(); });  // Succeeds or times out.
  EXPECT_TRUE(result->is_err());
}

TEST_F(GfxViewRefInstalledIntegrationTest, InstalledViewRef_ShouldReturnImmediately) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [control_ref, view_ref] = scenic::ViewRefPair::New();
  ViewRef view_ref_copy;
  fidl::Clone(view_ref, &view_ref_copy);
  scenic::View view(&root_session_->session, std::move(view_token), std::move(control_ref),
                    std::move(view_ref_copy), "root_view");
  AttachToScene(std::move(view_holder_token));
  BlockingPresent(root_session_->session);

  std::optional<WatchResult> result;
  view_ref_installed_ptr_->Watch(std::move(view_ref), [&result](auto watch_result) {
    result.emplace(std::move(watch_result));
  });
  RunLoopUntil([&result] { return result.has_value(); });  // Succeeds or times out.
  EXPECT_TRUE(result->is_response());
}

TEST_F(GfxViewRefInstalledIntegrationTest, WaitedOnViewRef_ShouldReturnWhenInstalled) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [control_ref, view_ref] = scenic::ViewRefPair::New();
  ViewRef view_ref_copy;
  fidl::Clone(view_ref, &view_ref_copy);

  std::optional<WatchResult> result;
  view_ref_installed_ptr_->Watch(std::move(view_ref), [&result](auto watch_result) {
    result.emplace(std::move(watch_result));
  });
  // Not installed; should not return yet.
  RunLoopWithTimeout(kWaitTime);
  EXPECT_FALSE(result.has_value());

  // Install it.
  scenic::View view(&root_session_->session, std::move(view_token), std::move(control_ref),
                    std::move(view_ref_copy), "root_view");
  AttachToScene(std::move(view_holder_token));
  BlockingPresent(root_session_->session);
  RunLoopUntil([&result] { return result.has_value(); });  // Succeeds or times out.
  EXPECT_TRUE(result->is_response());
}

TEST_F(GfxViewRefInstalledIntegrationTest, InstalledAndDisconnectedViewRef_ShouldReturnResponse) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [control_ref, view_ref] = scenic::ViewRefPair::New();
  ViewRef view_ref_copy;
  fidl::Clone(view_ref, &view_ref_copy);
  scenic::View view(&root_session_->session, std::move(view_token), std::move(control_ref),
                    std::move(view_ref_copy), "root_view");
  AttachToScene(std::move(view_holder_token));
  BlockingPresent(root_session_->session);

  // Disconnect the view from the scene.
  root_session_->scene.DetachChildren();
  BlockingPresent(root_session_->session);

  // Watch should still return true, since the view has been previously installed.
  std::optional<WatchResult> result;
  view_ref_installed_ptr_->Watch(std::move(view_ref), [&result](auto watch_result) {
    result.emplace(std::move(watch_result));
  });
  RunLoopUntil([&result] { return result.has_value(); });  // Succeeds or times out.
  EXPECT_TRUE(result->is_response());
}

TEST_F(GfxViewRefInstalledIntegrationTest, InstalledAndDestroyedViewRef_ShouldReturnError) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [control_ref, view_ref] = scenic::ViewRefPair::New();
  ViewRef view_ref_copy;
  fidl::Clone(view_ref, &view_ref_copy);
  {
    scenic::View view(&root_session_->session, std::move(view_token), std::move(control_ref),
                      std::move(view_ref_copy), "root_view");
    AttachToScene(std::move(view_holder_token));
    BlockingPresent(root_session_->session);
  }  // view out of scope here
  BlockingPresent(root_session_->session);

  std::optional<WatchResult> result;
  view_ref_installed_ptr_->Watch(std::move(view_ref), [&result](auto watch_result) {
    result.emplace(std::move(watch_result));
  });
  RunLoopUntil([&result] { return result.has_value(); });  // Succeeds or times out.
  EXPECT_TRUE(result->is_err());
}

// Check that transative connections are installed correctly.
TEST_F(GfxViewRefInstalledIntegrationTest, TransitiveConnection_ShouldReturnResponse) {
  // Create the root View.
  auto [root_view_token, root_view_holder_token] = scenic::ViewTokenPair::New();
  auto [root_control_ref, root_view_ref] = scenic::ViewRefPair::New();
  ViewRef root_view_ref_copy;
  fidl::Clone(root_view_ref, &root_view_ref_copy);
  scenic::View root_view(&root_session_->session, std::move(root_view_token),
                         std::move(root_control_ref), std::move(root_view_ref_copy), "root_view");

  // Create the child view and connect it to the parent, but don't attach to the scene yet.
  scenic::Session child_session = CreateSession(scenic(), {});
  auto [child_view_token, child_view_holder_token] = scenic::ViewTokenPair::New();
  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  ViewRef child_view_ref_copy;
  fidl::Clone(child_view_ref, &child_view_ref_copy);
  scenic::View child_view(&child_session, std::move(child_view_token), std::move(child_control_ref),
                          std::move(child_view_ref_copy), "child_view");

  scenic::ViewHolder child_view_holder(&root_session_->session, std::move(child_view_holder_token),
                                       "child_holder");
  root_view.AddChild(child_view_holder);
  BlockingPresent(child_session);
  BlockingPresent(root_session_->session);

  std::optional<WatchResult> result;
  view_ref_installed_ptr_->Watch(std::move(child_view_ref), [&result](auto watch_result) {
    result.emplace(std::move(watch_result));
  });
  // Not installed; should not return yet.
  RunLoopWithTimeout(kWaitTime);
  EXPECT_FALSE(result.has_value());

  // Now attach the whole thing to the scene and observe that the child view ref is installed.
  AttachToScene(std::move(root_view_holder_token));
  RunLoopUntil([&result] { return result.has_value(); });  // Succeeds or times out.
  EXPECT_TRUE(result->is_response());
}

}  // namespace integration_tests
