// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/svc/cpp/services.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>

#include <string>

#include "garnet/public/lib/gtest/real_loop_fixture.h"
#include "src/lib/syslog/cpp/logger.h"

// Maximum number of attempts to take a screenshot and find content.
static constexpr const uint32_t kMaxAttempts = 500;

// Pixel size of the square region to attempt to observe content within.
static constexpr const uint32_t kObservationDimension = 8;

static constexpr const char* kCameraDemoUrl =
    "fuchsia-pkg://fuchsia.com/camera_demo#meta/camera_demo.cmx";

class CameraDemoTest : public gtest::RealLoopFixture {
 protected:
  CameraDemoTest() : context_(sys::ComponentContext::Create()) {}
  ~CameraDemoTest() override {}
  static fit::function<void(zx_status_t)> MakeErrorHandler(std::string name) {
    return [name](zx_status_t status) {
      FX_PLOGS(ERROR, status) << name << " disconnected";
      ADD_FAILURE();
    };
  }

  virtual void SetUp() override {
    // Connect to the environment services.
    context_->svc()->Connect(launcher_.NewRequest());
    launcher_.set_error_handler(MakeErrorHandler("Launcher"));
    context_->svc()->Connect(presenter_.NewRequest());
    presenter_.set_error_handler(MakeErrorHandler("Presenter"));
    context_->svc()->Connect(scenic_.NewRequest());
    scenic_.set_error_handler(MakeErrorHandler("Scenic"));

    // Launch the demo.
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = kCameraDemoUrl;
    auto service_directory =
        sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
    launcher_->CreateComponent(std::move(launch_info), controller_.NewRequest());

    // Get its view provider.
    fuchsia::ui::app::ViewProviderPtr view_provider;
    zx_status_t status = service_directory->Connect(view_provider.NewRequest());
    ASSERT_EQ(status, ZX_OK);
    view_provider.set_error_handler(MakeErrorHandler("View Provider"));

    // Create a view token pair.
    fuchsia::ui::views::ViewHolderToken view_holder_token;
    zx::eventpair view_provider_token;
    status = zx::eventpair::create(0, &view_holder_token.value, &view_provider_token);
    ASSERT_EQ(status, ZX_OK);

    // Create a view using one token, and present it using the other.
    view_provider->CreateView(std::move(view_provider_token), nullptr, nullptr);
    presenter_->PresentView(std::move(view_holder_token), nullptr);

    RunLoopUntilIdle();
  }

  virtual void TearDown() override {
    if (controller_) {
      controller_->Kill();
      RunLoopUntilIdle();
      controller_ = nullptr;
    }
    scenic_ = nullptr;
    presenter_ = nullptr;
    launcher_ = nullptr;
  }

  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::ui::policy::PresenterPtr presenter_;
  fuchsia::sys::LauncherPtr launcher_;
  fuchsia::sys::ComponentControllerPtr controller_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
};

// Launch the demo and repeatedly take screenshots until content is observed.
TEST_F(CameraDemoTest, ContentVisible) {
  bool content_found = false;
  bool demo_terminated = false;

  // Watch for unexpected shutdowns.
  controller_.events().OnTerminated = [&](int64_t, fuchsia::sys::TerminationReason) {
    EXPECT_TRUE(content_found) << "Demo closed unexpectedly";
    demo_terminated = true;
  };

  for (uint32_t attempt = 0; attempt < kMaxAttempts && !content_found && !demo_terminated;
       ++attempt) {
    bool screenshot_returned = false;
    scenic_->TakeScreenshot([&](fuchsia::ui::scenic::ScreenshotData data, bool success) {
      screenshot_returned = true;
      if (!success) {
        // If scenic is still initializing, this will fail.
        return;
      }
      fzl::VmoMapper mapper;
      ASSERT_EQ(mapper.Map(data.data.vmo, 0, data.data.size, ZX_VM_PERM_READ), ZX_OK);
      for (uint32_t r = 0; r < kObservationDimension; ++r) {
        auto row_data = static_cast<char*>(mapper.start()) +
                        (data.info.height / 2 - kObservationDimension / 2 + r) * data.info.stride;
        for (uint32_t c = 0; c < kObservationDimension; ++c) {
          auto pixel_data = reinterpret_cast<uint32_t*>(
              row_data)[data.info.width / 2 - kObservationDimension / 2 + c];
          if (pixel_data != 0 && pixel_data != UINT32_MAX) {
            content_found = true;
          }
        }
      }
      mapper.Unmap();
    });
    while (!HasFatalFailure() && !screenshot_returned) {
      RunLoopUntilIdle();
    }
  }
  ASSERT_TRUE(content_found) << "Failed to detect content after " << kMaxAttempts
                             << " capture attempts";
}
