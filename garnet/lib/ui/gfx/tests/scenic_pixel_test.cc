// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

#include <gtest/gtest.h>
#include <lib/async/cpp/task.h>
#include <lib/component/cpp/testing/test_with_environment.h>
#include <lib/fsl/vmo/vector.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/ui/gfx/cpp/math.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>

namespace {

constexpr char kEnvironment[] = "ScenicPixelTest";
constexpr zx::duration kTimeout = zx::sec(15);

// These tests need Scenic and RootPresenter at minimum, which expand to the
// dependencies below. Using |TestWithEnvironment|, we use
// |fuchsia.sys.Environment| and |fuchsia.sys.Loader| from the system (declared
// in our *.cmx sandbox) and launch these other services in the environment we
// create in our test fixture.
//
// Another way to do this would be to whitelist these services in our sandbox
// and inject/start them via the |fuchsia.test| facet. However that has the
// disadvantage that it uses one instance of those services across all tests in
// the binary, making each test not hermetic wrt. the others. A trade-off is
// that the |TestWithEnvironment| method is more verbose.
const std::map<std::string, std::string> kServices = {
    {"fuchsia.tracelink.Registry",
     "fuchsia-pkg://fuchsia.com/trace_manager#meta/trace_manager.cmx"},
    {"fuchsia.ui.policy.Presenter2",
     "fuchsia-pkg://fuchsia.com/root_presenter#meta/root_presenter.cmx"},
    {"fuchsia.ui.scenic.Scenic",
     "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx"},
    {"fuchsia.vulkan.loader.Loader",
     "fuchsia-pkg://fuchsia.com/vulkan_loader#meta/vulkan_loader.cmx"}};

struct ViewContext {
  scenic::SessionPtrAndListenerRequest session_and_listener_request;
  zx::eventpair view_token;
};

// Represents a view that allows a callback to be set for its |Present|.
class TestView {
 public:
  virtual ~TestView() = default;
  virtual void set_present_callback(
      scenic::Session::PresentCallback present_callback) = 0;
};

// Test fixture that sets up an environment suitable for Scenic pixel tests
// and provides related utilities. The environment includes Scenic and
// RootPresenter, and their dependencies.
class ScenicPixelTest : public component::testing::TestWithEnvironment {
 protected:
  ScenicPixelTest() {
    std::unique_ptr<component::testing::EnvironmentServices> services =
        CreateServices();

    for (const auto& entry : kServices) {
      fuchsia::sys::LaunchInfo launch_info;
      launch_info.url = entry.second;
      services->AddServiceWithLaunchInfo(std::move(launch_info), entry.first);
    }

    environment_ =
        CreateNewEnclosingEnvironment(kEnvironment, std::move(services));

    environment_->ConnectToService(scenic_.NewRequest());
    scenic_.set_error_handler([this](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << status;
    });
  }

  // Blocking wrapper around |Scenic::TakeScreenshot|. This should not be called
  // from within a loop |Run|, as it spins up its own to block and nested loops
  // are undefined behavior.
  fuchsia::ui::scenic::ScreenshotData TakeScreenshot() {
    fuchsia::ui::scenic::ScreenshotData screenshot_out;
    scenic_->TakeScreenshot(
        [this, &screenshot_out](fuchsia::ui::scenic::ScreenshotData screenshot,
                                bool status) {
          EXPECT_TRUE(status) << "Failed to take screenshot";
          screenshot_out = std::move(screenshot);
          QuitLoop();
        });
    RunLoop();
    return screenshot_out;
  }

  // Dumps the screenshot to a shared temporary file and returns the filename.
  std::string DumpScreenshot(fuchsia::ui::scenic::ScreenshotData screenshot) {
    std::vector<uint8_t> data;
    EXPECT_TRUE(fsl::VectorFromVmo(screenshot.data, &data))
        << "Failed to read screenshot";
    const auto* test_info =
        testing::UnitTest::GetInstance()->current_test_info();
    std::ostringstream filename;
    filename << "/tmp/screenshot-" << test_info->test_case_name()
             << "::" << test_info->name() << "-" << screenshot_index_;

    std::ofstream fout(filename.str());
    EXPECT_TRUE(fout.good())
        << "Failed to open " << filename.str() << " for writing";

    // Convert to PPM. We'll probably need PNG instead for Skia Gold.
    fout << "P6\n"
         << screenshot.info.width << "\n"
         << screenshot.info.height << "\n"
         << 255 << "\n";

    const uint8_t* pchannel = data.data();
    for (uint32_t pixel = 0;
         pixel < screenshot.info.width * screenshot.info.height; pixel++) {
      uint8_t rgb[] = {pchannel[2], pchannel[1], pchannel[0]};
      fout.write(reinterpret_cast<const char*>(rgb), 3);
      pchannel += 4;
    }

    fout.flush();

    EXPECT_TRUE(fout.good())
        << "Failed to write screenshot to " << filename.str();

    ++screenshot_index_;
    return filename.str();
  }

  // Create a |ViewContext| that allows us to present a view via
  // |RootPresenter|. See also examples/ui/hello_base_view
  ViewContext CreatePresentationContext() {
    zx::eventpair view_holder_token, view_token;
    FXL_CHECK(zx::eventpair::create(0u, &view_holder_token, &view_token) ==
              ZX_OK);

    ViewContext view_context = {
        .session_and_listener_request =
            scenic::CreateScenicSessionPtrAndListenerRequest(scenic_.get()),
        .view_token = std::move(view_token),
    };

    fuchsia::ui::policy::Presenter2Ptr presenter;
    environment_->ConnectToService(presenter.NewRequest());
    presenter->PresentView(std::move(view_holder_token), nullptr);

    return view_context;
  }

  // Runs until the view renders its next frame. Technically, waits until the
  // |Present| callback is invoked with an expected presentation timestamp, and
  // then waits until that time.
  void RunUntilPresent(TestView* view) {
    // Typical sequence of events:
    // 1. We set up a view bound as a |SessionListener|.
    // 2. The view sends its initial |Present| to get itself connected, without
    //    a callback.
    // 3. We call |RunUntilPresent| which sets a present callback on our
    //    |TestView|.
    // 4. |RunUntilPresent| runs the message loop, which allows the view to
    //    receive a Scenic event telling us our metrics.
    // 5. In response, the view sets up the scene graph with the test scene.
    // 6. The view calls |Present| with the callback set in |RunUntilPresent|.
    // 7. The still-running message loop eventually dispatches the present
    //    callback.
    // 8. The callback schedules a quit for the presentation timestamp we got.
    // 9. The message loop eventually dispatches the quit and exits.

    view->set_present_callback([this](fuchsia::images::PresentationInfo info) {
      zx::time presentation_time =
          static_cast<zx::time>(info.presentation_time);
      FXL_LOG(INFO)
          << "Present scheduled for "
          << (presentation_time - zx::clock::get_monotonic()).to_msecs()
          << " ms from now";
      async::PostTaskForTime(dispatcher(), QuitLoopClosure(),
                             presentation_time);
    });

    EXPECT_FALSE(RunLoopWithTimeout(kTimeout))
        << "Timed out waiting for present. See surrounding logs for details.";
  }

  fuchsia::ui::scenic::ScenicPtr scenic_;

 private:
  std::unique_ptr<component::testing::EnclosingEnvironment> environment_;
  uint screenshot_index_ = 0;
};

struct Color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

inline bool operator==(const Color& a, const Color& b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

inline bool operator<(const Color& a, const Color& b) {
  return std::tie(a.r, a.g, a.b, a.a) < std::tie(b.r, b.g, b.b, b.a);
}

// RGBA hex dump
std::ostream& operator<<(std::ostream& os, const Color& c) {
  return os << fxl::StringPrintf("%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
}

// Base view with a solid background.
// See also lib/ui/base_view
class BackgroundView : public TestView,
                       private fuchsia::ui::scenic::SessionListener {
 public:
  static constexpr float kBackgroundElevation = 0.f;
  static constexpr Color kBackgroundColor = {0x67, 0x3a, 0xb7,
                                             0xff};  // Deep Purple 500

  BackgroundView(ViewContext context,
                 const std::string& debug_name = "BackgroundView")
      : binding_(this, std::move(context.session_and_listener_request.second)),
        session_(std::move(context.session_and_listener_request.first)),
        view_(&session_, std::move(context.view_token), debug_name),
        background_node_(&session_) {
    binding_.set_error_handler([](zx_status_t status) { FAIL() << status; });
    session_.Present(0, [](auto) {});

    scenic::Material background_material(&session_);
    background_material.SetColor(kBackgroundColor.r, kBackgroundColor.g,
                                 kBackgroundColor.b, kBackgroundColor.a);
    background_node_.SetMaterial(background_material);
    view_.AddChild(background_node_);
  }

  void set_present_callback(
      scenic::Session::PresentCallback present_callback) override {
    present_callback_ = std::move(present_callback);
  }

 protected:
  scenic::Session* session() { return &session_; }

  scenic::View* view() { return &view_; }

  virtual void Draw(float cx, float cy, float sx, float sy) {
    scenic::Rectangle background_shape(&session_, sx, sy);
    background_node_.SetShape(background_shape);
    background_node_.SetTranslationRH((float[]){cx, cy, -kBackgroundElevation});
  }

 private:
  void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) override {
    FXL_LOG(INFO) << "OnScenicEvent";
    for (const auto& event : events) {
      if (event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx &&
          event.gfx().Which() ==
              fuchsia::ui::gfx::Event::Tag::kViewPropertiesChanged) {
        const auto& evt = event.gfx().view_properties_changed();
        fuchsia::ui::gfx::BoundingBox layout_box =
            scenic::ViewPropertiesLayoutBox(evt.properties);

        const auto sz = scenic::Max(layout_box.max - layout_box.min, 0.f);
        OnViewPropertiesChanged(sz);
      }
    }
  }

  void OnScenicError(std::string error) override { FAIL() << error; }

  void OnViewPropertiesChanged(const fuchsia::ui::gfx::vec3& sz) {
    FXL_LOG(INFO) << "Metrics: " << sz.x << "x" << sz.y << "x" << sz.z;
    if (!sz.x || !sz.y || !sz.z)
      return;

    Draw(sz.x * .5f, sz.y * .5f, sz.x, sz.y);
    session_.Present(0, std::move(present_callback_));
  }

  fidl::Binding<fuchsia::ui::scenic::SessionListener> binding_;
  scenic::Session session_;
  scenic::View view_;

  scenic::ShapeNode background_node_;
  scenic::Session::PresentCallback present_callback_;
};

// Displays a static frame of the Spinning Square example.
// See also examples/ui/spinning_square
class RotatedSquareView : public BackgroundView {
 public:
  static constexpr float kSquareElevation = 8.f;
  static constexpr float kAngle = M_PI / 4;

  RotatedSquareView(ViewContext context,
                    const std::string& debug_name = "RotatedSquareView")
      : BackgroundView(std::move(context), debug_name),
        square_node_(session()) {
    scenic::Material square_material(session());
    square_material.SetColor(0xf5, 0x00, 0x57, 0xff);  // Pink A400
    square_node_.SetMaterial(square_material);
    view()->AddChild(square_node_);
  }

 private:
  void Draw(float cx, float cy, float sx, float sy) override {
    BackgroundView::Draw(cx, cy, sx, sy);

    const float square_size = std::min(sx, sy) * .6f;

    scenic::Rectangle square_shape(session(), square_size, square_size);
    square_node_.SetShape(square_shape);
    square_node_.SetTranslationRH((float[]){cx, cy, -kSquareElevation});
    square_node_.SetRotation(
        (float[]){0.f, 0.f, sinf(kAngle * .5f), cosf(kAngle * .5f)});
  }

  scenic::ShapeNode square_node_;
};

// Draws the following coordinate test pattern in a view:
//
// ___________________________________
// |                |                |
// |     BLACK      |        RED     |
// |           _____|_____           |
// |___________|  GREEN  |___________|
// |           |_________|           |
// |                |                |
// |      BLUE      |     MAGENTA    |
// |________________|________________|
//
class CoordinateTestView : public BackgroundView {
 public:
  CoordinateTestView(ViewContext context,
                     const std::string& debug_name = "RotatedSquareView")
      : BackgroundView(std::move(context), debug_name) {}

 private:
  void Draw(float cx, float cy, float sx, float sy) override {
    BackgroundView::Draw(cx, cy, sx, sy);

    scenic::EntityNode root_node(session());
    view()->AddChild(root_node);

    static const float pane_width = sx / 2;
    static const float pane_height = sy / 2;

    for (uint32_t i = 0; i < 2; i++) {
      for (uint32_t j = 0; j < 2; j++) {
        scenic::Rectangle pane_shape(session(), pane_width, pane_height);
        scenic::Material pane_material(session());
        pane_material.SetColor(i * 255.f, 0, j * 255.f, 255);

        scenic::ShapeNode pane_node(session());
        pane_node.SetShape(pane_shape);
        pane_node.SetMaterial(pane_material);
        pane_node.SetTranslationRH((i + 0.5) * pane_width,
                                   (j + 0.5) * pane_height, -20);
        root_node.AddChild(pane_node);
      }
    }

    scenic::Rectangle pane_shape(session(), sx / 4, sy / 4);
    scenic::Material pane_material(session());
    pane_material.SetColor(0, 255, 0, 255);

    scenic::ShapeNode pane_node(session());
    pane_node.SetShape(pane_shape);
    pane_node.SetMaterial(pane_material);
    pane_node.SetTranslationRH(0.5 * sx, 0.5 * sy, -40);
    root_node.AddChild(pane_node);
  }
};

TEST_F(ScenicPixelTest, SolidColor) {
  BackgroundView view(CreatePresentationContext());
  RunUntilPresent(&view);

  fuchsia::ui::scenic::ScreenshotData screenshot = TakeScreenshot();
  std::vector<uint8_t> data;
  EXPECT_TRUE(fsl::VectorFromVmo(screenshot.data, &data))
      << "Failed to read screenshot";

  EXPECT_GT(screenshot.info.width, 0u);
  EXPECT_GT(screenshot.info.height, 0u);

  // We could assert on each pixel individually, but a histogram might give us a
  // more meaningful failure.
  std::map<Color, size_t> histogram;

  // https://en.wikipedia.org/wiki/Sword_Art_Online_Alternative_Gun_Gale_Online#Characters
  const uint8_t* pchan = data.data();
  const size_t llenn = screenshot.info.width * screenshot.info.height;
  for (uint32_t pixel = 0; pixel < llenn; pixel++) {
    Color color = {pchan[2], pchan[1], pchan[0], pchan[3]};
    ++histogram[color];
    pchan += 4;
  }

  EXPECT_GT(histogram[BackgroundView::kBackgroundColor], 0u);
  histogram.erase(BackgroundView::kBackgroundColor);
  EXPECT_EQ((std::map<Color, size_t>){}, histogram) << "Unexpected colors";
}

// Renders a rotated square to the screen and saves a screenshot for later
// screendiff validation.
TEST_F(ScenicPixelTest, RotatedSquare) {
  RotatedSquareView view(CreatePresentationContext());
  RunUntilPresent(&view);

  const std::string filename = DumpScreenshot(TakeScreenshot());
  FXL_LOG(INFO) << "Wrote screenshot to " << filename;
}

TEST_F(ScenicPixelTest, ViewCoordinates) {
  // Synchronously get display dimensions
  float display_width;
  float display_height;
  scenic_->GetDisplayInfo([this, &display_width, &display_height](
      fuchsia::ui::gfx::DisplayInfo display_info) {
    display_width = static_cast<float>(display_info.width_in_px);
    display_height = static_cast<float>(display_info.height_in_px);
    QuitLoop();
  });
  RunLoop();

  CoordinateTestView view(CreatePresentationContext());
  RunUntilPresent(&view);

  fuchsia::ui::scenic::ScreenshotData screenshot = TakeScreenshot();
  std::vector<uint8_t> data;
  EXPECT_TRUE(fsl::VectorFromVmo(screenshot.data, &data))
      << "Failed to read screenshot";

  auto get_color_at_coordinates = [&display_width, &display_height, &data](
      float x, float y) -> Color {
    auto pixels = reinterpret_cast<Color*>(data.data());
    uint32_t index_x = x * display_width;
    uint32_t index_y = y * display_height;
    uint32_t index = index_y * display_width + index_x;
    return pixels[index];
  };

  EXPECT_EQ(Color({0, 0, 0, 255}), get_color_at_coordinates(.25f, .25f));
  EXPECT_EQ(Color({255, 0, 0, 255}), get_color_at_coordinates(.25f, .75f));
  EXPECT_EQ(Color({0, 0, 255, 255}), get_color_at_coordinates(.75f, .25f));
  EXPECT_EQ(Color({255, 0, 255, 255}), get_color_at_coordinates(.75f, .75f));
  EXPECT_EQ(Color({0, 255, 0, 255}), get_color_at_coordinates(.5f, .5f));
}

// Draws and tests the following coordinate test pattern without views:
// ___________________________________
// |                |                |
// |     BLACK      |        RED     |
// |           _____|_____           |
// |___________|  GREEN  |___________|
// |           |_________|           |
// |                |                |
// |      BLUE      |     MAGENTA    |
// |________________|________________|
//
TEST_F(ScenicPixelTest, GlobalCoordinates) {
  // Synchronously get display dimensions
  float display_width;
  float display_height;
  scenic_->GetDisplayInfo([this, &display_width, &display_height](
      fuchsia::ui::gfx::DisplayInfo display_info) {
    display_width = static_cast<float>(display_info.width_in_px);
    display_height = static_cast<float>(display_info.height_in_px);
    QuitLoop();
  });
  RunLoop();

  // Initialize session
  auto unique_session = std::make_unique<scenic::Session>(scenic_.get());
  auto session = unique_session.get();
  session->set_error_handler([this](zx_status_t status) {
    FXL_LOG(ERROR) << "Session terminated.";
    QuitLoop();
  });

  scenic::DisplayCompositor compositor(session);
  scenic::LayerStack layer_stack(session);
  scenic::Layer layer(session);
  scenic::Renderer renderer(session);
  scenic::Scene scene(session);
  scenic::Camera camera(scene);

  float eye_position[3] = {display_width / 2.f, display_height / 2.f, -1001};
  float look_at[3] = {display_width / 2.f, display_height / 2.f, 1};
  float up[3] = {0, -1, 0};
  camera.SetTransform(eye_position, look_at, up);

  compositor.SetLayerStack(layer_stack);
  layer_stack.AddLayer(layer);
  layer.SetSize(display_width, display_height);
  layer.SetRenderer(renderer);
  renderer.SetCamera(camera.id());

  // Set up lights.
  scenic::AmbientLight ambient_light(session);
  scene.AddLight(ambient_light);
  ambient_light.SetColor(1.f, 1.f, 1.f);

  // Create an EntityNode to serve as the scene root.
  scenic::EntityNode root_node(session);
  scene.AddChild(root_node.id());

  static const float pane_width = display_width / 2;
  static const float pane_height = display_height / 2;

  for (uint32_t i = 0; i < 2; i++) {
    for (uint32_t j = 0; j < 2; j++) {
      scenic::Rectangle pane_shape(session, pane_width, pane_height);
      scenic::Material pane_material(session);
      pane_material.SetColor(i * 255.f, 0, j * 255.f, 255);

      scenic::ShapeNode pane_node(session);
      pane_node.SetShape(pane_shape);
      pane_node.SetMaterial(pane_material);
      pane_node.SetTranslationRH((i + 0.5) * pane_width,
                                 (j + 0.5) * pane_height, -20);
      root_node.AddChild(pane_node);
    }
  }

  scenic::Rectangle pane_shape(session, display_width / 4, display_height / 4);
  scenic::Material pane_material(session);
  pane_material.SetColor(0, 255, 0, 255);

  scenic::ShapeNode pane_node(session);
  pane_node.SetShape(pane_shape);
  pane_node.SetMaterial(pane_material);
  pane_node.SetTranslationRH(0.5 * display_width, 0.5 * display_height, -40);
  root_node.AddChild(pane_node);

  // Actual tests. Test the same scene with an orthographic and perspective
  // camera.
  std::string camera_type[2] = {"orthographic", "perspective"};
  float fov[2] = {0, 2 * atan((display_height / 2.f) / abs(eye_position[2]))};

  for (int i = 0; i < 2; i++) {
    FXL_LOG(INFO) << "Testing " << camera_type[i] << " camera";
    camera.SetProjection(fov[i]);

    session->Present(
        0, [this](fuchsia::images::PresentationInfo info) { QuitLoop(); });
    RunLoop();

    fuchsia::ui::scenic::ScreenshotData screenshot = TakeScreenshot();
    std::vector<uint8_t> data;
    EXPECT_TRUE(fsl::VectorFromVmo(screenshot.data, &data))
        << "Failed to read screenshot";

    auto get_color_at_coordinates = [&display_width, &display_height, &data](
        float x, float y) -> Color {
      auto pixels = reinterpret_cast<Color*>(data.data());
      uint32_t index_x = x * display_width;
      uint32_t index_y = y * display_height;
      uint32_t index = index_y * display_width + index_x;
      return pixels[index];
    };

    EXPECT_EQ(Color({0, 0, 0, 255}), get_color_at_coordinates(.25f, .25f));
    EXPECT_EQ(Color({255, 0, 0, 255}), get_color_at_coordinates(.25f, .75f));
    EXPECT_EQ(Color({0, 0, 255, 255}), get_color_at_coordinates(.75f, .25f));
    EXPECT_EQ(Color({255, 0, 255, 255}), get_color_at_coordinates(.75f, .75f));
    EXPECT_EQ(Color({0, 255, 0, 255}), get_color_at_coordinates(.5f, .5f));
  }
}

}  // namespace