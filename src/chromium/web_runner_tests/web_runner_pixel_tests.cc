// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fdio/spawn.h>
#include <lib/fit/function.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <iomanip>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/chromium/web_runner_tests/mock_get.h"
#include "src/chromium/web_runner_tests/test_server.h"
#include "src/chromium/web_runner_tests/web_context.h"
#include "src/lib/fsl/vmo/vector.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/ui/base_view/embedded_view_utils.h"
#include "src/ui/testing/views/embedder_view.h"

namespace {

// Max time to wait in failure cases before bailing.
constexpr zx::duration kTimeout = zx::sec(15);
constexpr uint32_t kBlankColor = 0x00000000;

constexpr zx::duration kTestTimeout = zx::sec(60);

std::map<uint32_t, size_t> Histogram(const fuchsia::ui::scenic::ScreenshotData& screenshot) {
  EXPECT_GT(screenshot.info.width, 0u);
  EXPECT_GT(screenshot.info.height, 0u);

  std::vector<uint8_t> data;
  EXPECT_TRUE(fsl::VectorFromVmo(screenshot.data, &data)) << "Failed to read screenshot";

  std::map<uint32_t, size_t> histogram;
  const uint32_t* bitmap = reinterpret_cast<const uint32_t*>(data.data());
  const size_t size = screenshot.info.width * screenshot.info.height;
  EXPECT_EQ(size * sizeof(uint32_t), data.size());
  for (size_t i = 0; i < size; ++i) {
    ++histogram[bitmap[i]];
  }

  return histogram;
}

// Invokes the input tool for input injection.
// See src/ui/tools/input/README.md or `input --help` for usage details.
// Commands used here:
//  * tap <x> <y> (scaled out of 1000)
// TODO(fxbug.dev/24462): Expose as a FIDL service.
void Input(std::vector<const char*> args) {
  // start with proc name, end with nullptr
  args.insert(args.begin(), "input");
  args.push_back(nullptr);

  zx_handle_t proc;
  zx_status_t status =
      fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, "/bin/input", args.data(), &proc);
  FX_CHECK(status == ZX_OK) << "fdio_spawn: " << zx_status_get_string(status);

  status = zx_object_wait_one(proc, ZX_PROCESS_TERMINATED,
                              (zx::clock::get_monotonic() + kTimeout).get(), nullptr);
  FX_CHECK(status == ZX_OK) << "zx_object_wait_one: " << zx_status_get_string(status);

  zx_info_process_t info;
  status = zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
  FX_CHECK(status == ZX_OK) << "zx_object_get_info: " << zx_status_get_string(status);
  FX_CHECK(info.return_code == 0) << info.return_code;
}

// Base fixture for pixel tests, containing Scenic and presentation setup, and
// screenshot utilities.
class PixelTest : public gtest::RealLoopFixture {
 protected:
  PixelTest() : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {
    scenic_ = context_->svc()->Connect<fuchsia::ui::scenic::Scenic>();
    scenic_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
    });

    // TODO(fxbug.dev/40933)
    //
    // These tests can flake when a screenshot captures a frame from the previous test, which can
    // advance the test logic early. This is a temporary solution that waits for a blank on setup.
    // Better solutions include hermetic Scenic (using src/ui/scenic/lib/gfx/tests/pixel_test.h) or
    // refactoring to use view state events (probably the best solution; greatly improves
    // determinism at the expense of added harness complexity).
    //
    // WebRunnerPixelTest.Static below is factored to use view state events, but the others are not.
    FX_CHECK(WaitForBlank());
  }

  sys::ComponentContext* context() { return context_.get(); }
  fuchsia::ui::scenic::Scenic* scenic() { return scenic_.get(); }

  // Gets a view token for presentation by |RootPresenter|. See also
  // garnet/examples/ui/hello_base_view
  fuchsia::ui::views::ViewToken CreatePresentationViewToken() {
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

    auto presenter = context_->svc()->Connect<fuchsia::ui::policy::Presenter>();
    presenter.set_error_handler(
        [](zx_status_t status) { FAIL() << "presenter: " << zx_status_get_string(status); });
    presenter->PresentOrReplaceView(std::move(view_holder_token), nullptr);

    return std::move(view_token);
  }

  bool ScreenshotUntil(fit::function<bool(fuchsia::ui::scenic::ScreenshotData, bool)> condition,
                       zx::duration timeout = kTimeout) {
    zx::time start = zx::clock::get_monotonic();
    while (zx::clock::get_monotonic() - start <= timeout) {
      fuchsia::ui::scenic::ScreenshotData screenshot;
      bool ok;
      scenic_->TakeScreenshot(
          [this, &screenshot, &ok](fuchsia::ui::scenic::ScreenshotData screenshot_in, bool status) {
            ok = status;
            screenshot = std::move(screenshot_in);
            QuitLoop();
          });

      if (!RunLoopWithTimeout(timeout) && condition(std::move(screenshot), ok)) {
        return true;
      }
    }

    return false;
  }

  // Blank can manifest as invalid screenshots or blackness.
  // TODO(fxbug.dev/40933): remove
  bool WaitForBlank() {
    return ScreenshotUntil([](fuchsia::ui::scenic::ScreenshotData screenshot, bool status) {
      return !status || Histogram(screenshot)[kBlankColor] > 0u;
    });
  }

  void ExpectSolidColor(uint32_t argb) {
    std::map<uint32_t, size_t> histogram;

    FX_LOGS(INFO) << "Looking for color " << std::hex << argb << ".";
    EXPECT_TRUE(ScreenshotUntil(
        [argb, &histogram](fuchsia::ui::scenic::ScreenshotData screenshot, bool status) {
          if (!status)
            return false;

          histogram = Histogram(screenshot);
          FX_LOGS(INFO) << "Looking for color " << std::hex << argb << ": found " << std::dec
                        << histogram[argb] << " px.";
          return histogram[argb] > 0u;
        }));

    histogram.erase(argb);
    EXPECT_EQ((std::map<uint32_t, size_t>){}, histogram) << "Unexpected colors";
  }

  void ExpectPrimaryColor(uint32_t color) {
    std::multimap<size_t, uint32_t> inverse_histogram;

    FX_LOGS(INFO) << "Looking for color " << std::hex << color;
    EXPECT_TRUE(ScreenshotUntil([color, &inverse_histogram](
                                    fuchsia::ui::scenic::ScreenshotData screenshot, bool status) {
      if (!status)
        return false;

      std::map<uint32_t, size_t> histogram = Histogram(screenshot);
      FX_LOGS(INFO) << histogram[color] << " px";

      inverse_histogram.clear();
      for (const auto entry : histogram) {
        inverse_histogram.emplace(entry.second, entry.first);
      }

      return (--inverse_histogram.end())->second == color;
    })) << "Primary color: "
        << std::hex << (--inverse_histogram.end())->second;
  }

 private:
  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::sys::ComponentControllerPtr runner_ctrl_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
};

using WebRunnerPixelTest = PixelTest;

// Loads a static page with a solid color via the component framework and
// verifies that the color is the only color onscreen.
TEST_F(WebRunnerPixelTest, Static) {
  static constexpr uint32_t kTargetColor = 0xffff00ff;

  web_runner_tests::TestServer server;
  FX_CHECK(server.FindAndBindPort());

  // Chromium and the Fuchsia network package loader both send us requests. This
  // may go away after MI4-1807; although the race seems to be in Modular, the
  // fix may remove the unnecessary net request in component framework.
  auto serve = server.ServeAsync([&server] {
    while (server.Accept()) {
      web_runner_tests::MockHttpGetResponse(&server, "static.html");
    }
  });

  fuchsia::sys::ComponentControllerPtr controller;
  fuchsia::sys::LauncherPtr launcher;
  context()->svc()->Connect(launcher.NewRequest());

  auto info = scenic::LaunchComponentAndCreateView(
      launcher, fxl::StringPrintf("http://localhost:%d/static.html", server.port()), {});

  info.controller.events().OnTerminated = [](int64_t code, fuchsia::sys::TerminationReason reason) {
    FAIL();
  };

  // Present the view.
  scenic::EmbedderView embedder_view({
      .session_and_listener_request = scenic::CreateScenicSessionPtrAndListenerRequest(scenic()),
      .view_token = CreatePresentationViewToken(),
  });

  embedder_view.EmbedView(
      std::move(info),
      /*view_state_changed_callback=*/[this](fuchsia::ui::gfx::ViewState view_state) {
        EXPECT_TRUE(view_state.is_rendering);
        QuitLoop();
      });

  // Wait to get a signal that the view is being rendered.
  EXPECT_FALSE(RunLoopWithTimeout(kTestTimeout))
      << "Timed out waiting for a ViewStateChanged event.";

  ExpectSolidColor(kTargetColor);
}

// This fixture uses fuchsia.web FIDL services to interact with the WebEngine.
class WebPixelTest : public PixelTest {
 protected:
  WebPixelTest()
      : web_context_(context()),
        embedder_view_({
            .session_and_listener_request =
                scenic::CreateScenicSessionPtrAndListenerRequest(scenic()),
            .view_token = CreatePresentationViewToken(),
        }) {
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

    web_context_.web_frame()->CreateView(std::move(view_token));

    scenic::EmbeddedViewInfo embedded_view_info;
    embedded_view_info.view_holder_token = std::move(view_holder_token);
    embedder_view_.EmbedView(
        std::move(embedded_view_info),
        /*view_state_changed_callback=*/[this](fuchsia::ui::gfx::ViewState view_state) {
          EXPECT_TRUE(view_state.is_rendering);
          QuitLoop();
        });

    // Wait to get a signal that the view is being rendered.
    EXPECT_FALSE(RunLoopWithTimeout(kTestTimeout))
        << "Timed out waiting for a ViewStateChanged event.";
  }

  WebContext* web_context() { return &web_context_; }

 private:
  WebContext web_context_;
  scenic::EmbedderView embedder_view_;
};

// Loads a static page with a solid color via fuchsia.web interfaces and
// verifies that the color is the only color onscreen.
TEST_F(WebPixelTest, Static) {
  static constexpr uint32_t kTargetColor = 0xffff00ff;

  web_runner_tests::TestServer server;
  FX_CHECK(server.FindAndBindPort());

  auto serve = server.ServeAsync([&server] {
    FX_LOGS(INFO) << "Waiting for HTTP request from Chromium";
    ASSERT_TRUE(server.Accept()) << "Did not receive HTTP request from Chromium";
    web_runner_tests::MockHttpGetResponse(&server, "static.html");
  });

  web_context()->Navigate(fxl::StringPrintf("http://localhost:%d/static.html", server.port()));
  ExpectSolidColor(kTargetColor);
}

// Loads a dynamic page that starts with a Fuchsia background. This test verifies the initial color,
// taps on the view, and verifies that the color changed.
TEST_F(WebPixelTest, Dynamic) {
  static constexpr uint32_t kBeforeColor = 0xffff00ff;
  static constexpr uint32_t kAfterColor = 0xff40e0d0;

  web_runner_tests::TestServer server;
  FX_CHECK(server.FindAndBindPort());

  auto serve = server.ServeAsync([&server] {
    ASSERT_TRUE(server.Accept());
    web_runner_tests::MockHttpGetResponse(&server, "dynamic.html");
  });

  web_context()->Navigate(fxl::StringPrintf("http://localhost:%d/dynamic.html", server.port()));

  ExpectPrimaryColor(kBeforeColor);
  Input({"tap", "500", "125"});  // centered in top quarter of screen
  ExpectPrimaryColor(kAfterColor);
}

}  // namespace
