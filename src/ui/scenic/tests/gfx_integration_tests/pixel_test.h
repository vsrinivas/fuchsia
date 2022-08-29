// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_TESTS_GFX_INTEGRATION_TESTS_PIXEL_TEST_H_
#define SRC_UI_SCENIC_TESTS_GFX_INTEGRATION_TESTS_PIXEL_TEST_H_

#include <fuchsia/ui/annotation/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/lib/ui/base_view/base_view.h"
#include "src/ui/testing/views/color.h"
#include "src/ui/testing/views/test_view.h"

namespace integration_tests {

using RealmRoot = component_testing::RealmRoot;

static constexpr float kDefaultCameraOffset = 1001;
constexpr zx::duration kPresentTimeout = zx::sec(15), kIndirectPresentTimeout = zx::sec(90),
                       kScreenshotTimeout = zx::sec(15);

struct DisplayDimensions {
  float width, height;
};

struct RootSession {
  RootSession(fuchsia::ui::scenic::Scenic* scenic, const DisplayDimensions& display_dimensions)
      : session(scenic),
        compositor(&session),
        layer_stack(&session),
        layer(&session),
        renderer(&session),
        scene(&session),
        camera(scene),
        display_dimensions(display_dimensions),
        ambient_light(&session) {
    compositor.SetLayerStack(layer_stack);
    layer_stack.AddLayer(layer);
    layer.SetRenderer(renderer);
    layer.SetSize(display_dimensions.width, display_dimensions.height);
    renderer.SetCamera(camera);
    scene.AddLight(ambient_light);
    ambient_light.SetColor(1.f, 1.f, 1.f);
  }

  // Sets up a camera at (x, y) = (width / 2, height / 2) looking at +z such
  // that the near plane is at -1000 and the far plane is at 0.
  //
  // Note that the ortho camera (fov = 0) ignores the transform and is
  // effectively always set this way.
  template <typename Camera = scenic::Camera>
  Camera SetUpCamera(float offset = kDefaultCameraOffset) {
    // fxbug.dev/24474: The near plane is hardcoded at -1000 and far at 0 in camera
    // space.
    const std::array<float, 3> eye_position = {display_dimensions.width / 2.f,
                                               display_dimensions.height / 2.f, -offset};
    const std::array<float, 3> look_at = {display_dimensions.width / 2.f,
                                          display_dimensions.height / 2.f, 1};
    static const std::array<float, 3> up = {0, -1, 0};
    Camera camera(scene);
    camera.SetTransform(eye_position, look_at, up);
    renderer.SetCamera(camera.id());
    return camera;
  }

  scenic::Session session;
  scenic::DisplayCompositor compositor;
  scenic::LayerStack layer_stack;
  scenic::Layer layer;
  scenic::Renderer renderer;
  scenic::Scene scene;
  scenic::Camera camera;
  const DisplayDimensions display_dimensions;
  scenic::AmbientLight ambient_light;
  std::unique_ptr<scenic::ViewHolder> view_holder;
};

// Test fixture that sets up an environment suitable for Scenic pixel tests
// and provides related utilities. The environment includes Scenic and
// RootPresenter, and their dependencies.
class PixelTest : public gtest::RealLoopFixture {
 protected:
  fuchsia::ui::scenic::Scenic* scenic() { return scenic_.get(); }

  // |testing::Test|
  void SetUp() override;

  // Blocking call to |fuchsia::ui::scenic::Scenic::GetDisplayInfo|.
  DisplayDimensions GetDisplayDimensions();

  // Blocking call to |scenic::Session::Present|.
  void Present(scenic::Session* session, zx::time present_time = zx::time(0));

  // Blocking wrapper around |Scenic::TakeScreenshot|. This should not be called
  // from within a loop |Run|, as it spins up its own to block and nested loops
  // are undefined behavior.
  scenic::Screenshot TakeScreenshot();

  // Sets the next Present-callback that will be used, then waits for some event on the looper
  // (usually OnScenicEvent) to trigger another Present, and then waits for THAT Present to have its
  // callback return.
  // TODO(fxbug.dev/42422): This is too unintuitive. Rewrite to be clearer.
  void RunUntilIndirectPresent(scenic::TestView* view);

  // As an alternative to using RootPresenter, tests can set up their own
  // session. This offers more control over the camera and compositor.
  std::unique_ptr<RootSession> SetUpTestSession();

  std::unique_ptr<RealmRoot>& realm() { return realm_; }

  fuchsia::ui::annotation::RegistryPtr& annotation_registry() { return annotation_registry_; }

 private:
  // Sets up the realm topology and exposes the required protocols to the test fixture component.
  virtual RealmRoot SetupRealm() = 0;

  std::unique_ptr<RealmRoot> realm_;

  fuchsia::ui::annotation::RegistryPtr annotation_registry_;

  fuchsia::ui::scenic::ScenicPtr scenic_;
};

}  // namespace integration_tests

#endif  // SRC_UI_SCENIC_TESTS_GFX_INTEGRATION_TESTS_PIXEL_TEST_H_
