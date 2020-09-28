// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_TESTS_PIXEL_TEST_H_
#define SRC_UI_SCENIC_LIB_GFX_TESTS_PIXEL_TEST_H_

#include <fuchsia/ui/annotation/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/zx/time.h>

#include <memory>
#include <string>

#include "src/lib/ui/base_view/base_view.h"
#include "src/ui/testing/views/color.h"
#include "src/ui/testing/views/test_view.h"

namespace gfx {

struct DisplayDimensions {
  float width, height;
};

struct TestSession {
  static constexpr float kDefaultCameraOffset = 1001;

  TestSession(fuchsia::ui::scenic::Scenic* scenic, const DisplayDimensions& display_dimensions);

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
  const DisplayDimensions display_dimensions;
  scenic::DisplayCompositor compositor;
  scenic::LayerStack layer_stack;
  scenic::Layer layer;
  scenic::Renderer renderer;
  scenic::Scene scene;
  scenic::AmbientLight ambient_light;
};

// Test fixture that sets up an environment suitable for pixel tests and provides related utilities.
// By default, the environment includes Scenic, RootPresenter, and their dependencies.
class PixelTest : public sys::testing::TestWithEnvironment {
 protected:
  PixelTest(const std::string& environment_label);

  fuchsia::ui::scenic::Scenic* scenic() { return scenic_.get(); }

  fuchsia::ui::annotation::Registry* annotation_registry() { return annotation_registry_.get(); }

  // Sets up the enclosing environment, calling |CreateServices()| to configure services.
  // |testing::Test|
  void SetUp() override;

  // Configures services available to the test environment. This method is called by |SetUp()|. It
  // shadows but calls |TestWithEnvironment::CreateServices()|. In addition the default
  // implementation wires up Scenic, RootPresenter, and their dependencies.
  virtual std::unique_ptr<sys::testing::EnvironmentServices> CreateServices();

  // Blocking wrapper around |Scenic::TakeScreenshot|. This should not be called
  // from within a loop |Run|, as it spins up its own to block and nested loops
  // are undefined behavior.
  scenic::Screenshot TakeScreenshot();

  // Gets a view token for presentation by |RootPresenter|. See also
  // src/ui/examples/hello_base_view
  fuchsia::ui::views::ViewToken CreatePresentationViewToken(bool clobber);

  // Create a |ViewContext| that allows us to present a view via
  // |RootPresenter|. See also examples/ui/hello_base_view
  scenic::ViewContext CreatePresentationContext(bool clobber = false);

  // Sets the next Present-callback that will be used, then waits for some event on the looper
  // (usually OnScenicEvent) to trigger another Present, and then waits for THAT Present to have its
  // callback return.
  // TODO(fxbug.dev/42422): This is too unintuitive. Rewrite to be clearer.
  void RunUntilIndirectPresent(scenic::TestView* view);

  // Blocking call to |scenic::Session::Present|.
  void Present(scenic::Session* session, zx::time present_time = zx::time(0));

  // Blocking call to |fuchsia::ui::scenic::Scenic::GetDisplayInfo|.
  DisplayDimensions GetDisplayDimensions();

  // As an alternative to using RootPresenter, tests can set up their own
  // session. This offers more control over the camera and compositor.
  std::unique_ptr<TestSession> SetUpTestSession();

 protected:
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;

 private:
  const std::string environment_label_;

  fuchsia::ui::annotation::RegistryPtr annotation_registry_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
};

}  // namespace gfx
#endif  // SRC_UI_SCENIC_LIB_GFX_TESTS_PIXEL_TEST_H_
