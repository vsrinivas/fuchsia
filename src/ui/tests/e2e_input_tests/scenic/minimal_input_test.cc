// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/spawn.h>
#include <lib/fit/function.h>
#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>

#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/ui/base_view/base_view.h"

// NOTE WELL. Run each of these e2e tests in its own executable.  They each
// consume and maintain process-global context, so it's better to keep them
// separate.  Plus, separation means they start up components in a known good
// state, instead of reusing component state possibly dirtied by other tests.

namespace {

using fuchsia::ui::input::InputEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

// Shared context for all tests in this process.
// Set it up once, never delete it.
sys::ComponentContext* g_context = nullptr;

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

// A very small Scenic client. Puts up a fuchsia-colored rectangle, and stores
// input events for examination.
class MinimalClientView : public scenic::BaseView {
 public:
  MinimalClientView(scenic::ViewContext context, fit::function<void()> on_view_attached_to_scene,
                    fit::function<void(const std::vector<InputEvent>&)> on_terminate,
                    async_dispatcher_t* dispatcher)
      : scenic::BaseView(std::move(context), "MinimalClientView"),
        on_view_attached_to_scene_(std::move(on_view_attached_to_scene)),
        on_terminate_(std::move(on_terminate)),
        dispatcher_(dispatcher) {
    FXL_CHECK(dispatcher_);
  }

  void CreateScene(uint32_t width_in_px, uint32_t height_in_px) {
    float width = static_cast<float>(width_in_px);
    float height = static_cast<float>(height_in_px);

    scenic::ShapeNode background(session());

    scenic::Material material(session());
    material.SetColor(255, 0, 255, 255);  // Fuchsia
    background.SetMaterial(material);

    scenic::Rectangle rectangle(session(), width, height);
    background.SetShape(rectangle);
    background.SetTranslation(width / 2, height / 2, -10.f);

    root_node().AddChild(background);
  }

  // |scenic::BaseView|
  void OnPropertiesChanged(fuchsia::ui::gfx::ViewProperties old_properties) override {
    if (has_logical_size()) {
      CreateScene(logical_size().x, logical_size().y);
      session()->Present(zx_clock_get_monotonic(),
                         [](auto info) { FXL_LOG(INFO) << "Client: scene created."; });
    }
  }

  // |scenic::BaseView|
  void OnScenicEvent(fuchsia::ui::scenic::Event event) override {
    if (event.is_gfx() && event.gfx().is_view_attached_to_scene()) {
      // TODO(fxb/41382): Remove this extra Present() call. Today we need it to ensure the ViewTree
      // connection gets flushed on time.
      session()->Present(zx_clock_get_monotonic(), [this](auto info) {
        // When view is connected to scene (a proxy for "has rendered"), trigger input injection.
        FXL_LOG(INFO) << "Client: view attached to scene.";
        FXL_CHECK(on_view_attached_to_scene_) << "on_view_attached_to_scene_ was not set!";
        on_view_attached_to_scene_();
      });
    }
  }

  // |scenic::BaseView|
  void OnInputEvent(InputEvent event) override {
    // Simple termination condition: Last event of first gesture.
    if (event.is_pointer() && event.pointer().phase == Phase::REMOVE) {
      async::PostTask(dispatcher_, [this] {
        FXL_LOG(INFO) << "Client: all expected inputs received.";
        FXL_CHECK(on_terminate_) << "on_terminate_ was not set!";
        on_terminate_(observed_);
      });
    }

    // Store inputs for checking later.
    observed_.push_back(std::move(event));
  }

 private:
  // |scenic::SessionListener|
  void OnScenicError(std::string error) override { FXL_LOG(FATAL) << error; }

  fit::function<void()> on_view_attached_to_scene_;
  fit::function<void(const std::vector<InputEvent>&)> on_terminate_;

  std::vector<InputEvent> observed_;

  async_dispatcher_t* dispatcher_ = nullptr;
};

class MinimalInputTest : public gtest::RealLoopFixture {
 protected:
  MinimalInputTest() {
    // This fixture constructor may run multiple times, but we want the context
    // to be set up just once per process.
    if (g_context == nullptr) {
      g_context = sys::ComponentContext::Create().release();
    }
  }

  ~MinimalInputTest() override {
    FXL_CHECK(injection_count_ == 1) << "Oops, didn't actually do anything.";
  }

  void InjectInput(std::vector<const char*> args) {
    // Start with process name, end with nullptr.
    args.insert(args.begin(), "input");
    args.push_back(nullptr);

    // Start the /bin/input process.
    zx_handle_t proc;
    zx_status_t status =
        fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, "/bin/input", args.data(), &proc);
    FXL_CHECK(status == ZX_OK) << "fdio_spawn: " << zx_status_get_string(status);

    // Wait for termination.
    status = zx_object_wait_one(proc, ZX_PROCESS_TERMINATED,
                                (zx::clock::get_monotonic() + kTimeout).get(), nullptr);
    FXL_CHECK(status == ZX_OK) << "zx_object_wait_one: " << zx_status_get_string(status);

    // Check termination status.
    zx_info_process_t info;
    status = zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
    FXL_CHECK(status == ZX_OK) << "zx_object_get_info: " << zx_status_get_string(status);
    FXL_CHECK(info.return_code == 0) << "info.return_code: " << info.return_code;
  }

  fuchsia::ui::policy::PresenterPtr root_presenter_;
  fuchsia::ui::scenic::ScenicPtr scenic_;

  std::unique_ptr<MinimalClientView> view_;
  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;

  uint32_t injection_count_ = 0;
};

TEST_F(MinimalInputTest, Tap) {
  // Set up inputs. Fires when client view's content is connected to the scene.
  fit::function<void()> on_view_attached_to_scene =
      [this] {
        FXL_LOG(INFO) << "Client: injecting input.";
        FXL_CHECK(display_width_ > 0 && display_height_ > 0) << "precondition";
        {
          std::string x = std::to_string(display_width_ / 2);
          std::string y = std::to_string(display_height_ / 2);
          std::string width = "--width=" + std::to_string(display_width_);
          std::string height = "--height=" + std::to_string(display_height_);
          InjectInput({"tap",  // Tap at the center of the display
                       x.c_str(), y.c_str(), width.c_str(), height.c_str(), nullptr});
        }
        ++injection_count_;
      };

  // Set up expectations. Fires when we see the "quit" condition.
  fit::function<void(const std::vector<InputEvent>&)> on_terminate =
      [this](const std::vector<InputEvent>& observed) {
        if (FXL_VLOG_IS_ON(2)) {
          for (const auto& event : observed) {
            FXL_LOG(INFO) << "Input event observed: " << event;
          }
        }

        ASSERT_EQ(observed.size(), 5u);

        ASSERT_TRUE(observed[0].is_pointer());
        EXPECT_EQ(observed[0].pointer().phase, Phase::ADD);

        ASSERT_TRUE(observed[1].is_focus());
        EXPECT_TRUE(observed[1].focus().focused);

        ASSERT_TRUE(observed[2].is_pointer());
        EXPECT_EQ(observed[2].pointer().phase, Phase::DOWN);

        ASSERT_TRUE(observed[3].is_pointer());
        EXPECT_EQ(observed[3].pointer().phase, Phase::UP);

        ASSERT_TRUE(observed[4].is_pointer());
        EXPECT_EQ(observed[4].pointer().phase, Phase::REMOVE);

        QuitLoop();
        // TODO(SCN-1449): Cleanly break the View/ViewHolder connection.
      };

  // Connect to Scenic, park a callback to obtain display dimensions.
  scenic_ = g_context->svc()->Connect<fuchsia::ui::scenic::Scenic>();
  scenic_.set_error_handler([](zx_status_t status) {
    FXL_LOG(FATAL) << "Lost connection to Scenic: " << zx_status_get_string(status);
  });
  scenic_->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo display_info) {
    display_width_ = display_info.width_in_px;
    display_height_ = display_info.height_in_px;

    FXL_CHECK(display_width_ > 0 && display_height_ > 0)
        << "Display size unsuitable for this test: (" << display_width_ << ", " << display_height_
        << ").";
  });

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  // Connect to RootPresenter, create a ViewHolder.
  root_presenter_ = g_context->svc()->Connect<fuchsia::ui::policy::Presenter>();
  root_presenter_.set_error_handler([](zx_status_t status) {
    FXL_LOG(FATAL) << "Lost connection to RootPresenter: " << zx_status_get_string(status);
  });
  root_presenter_->PresentView(std::move(view_holder_token), nullptr);

  // Create a View.
  scenic::ViewContext view_context = {
      .session_and_listener_request =
          scenic::CreateScenicSessionPtrAndListenerRequest(scenic_.get()),
      .view_token = std::move(view_token),
      .component_context = g_context,
  };
  view_ = std::make_unique<MinimalClientView>(std::move(view_context),
                                              std::move(on_view_attached_to_scene),
                                              std::move(on_terminate), dispatcher());

  // Post a "just in case" quit task, if the test hangs.
  async::PostDelayedTask(
      dispatcher(),
      [] { FXL_LOG(FATAL) << "\n\n>> Test did not complete in time, terminating. <<\n\n"; },
      kTimeout);

  RunLoop();  // Go!
}

}  // namespace

// NOTE: We link in FXL's gtest_main to enable proper logging.
