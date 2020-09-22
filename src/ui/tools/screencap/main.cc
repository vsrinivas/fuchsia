// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include <cstdlib>
#include <iostream>
#include <memory>

#include "src/lib/fsl/vmo/vector.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

class ScreenshotTaker {
 public:
  explicit ScreenshotTaker(async::Loop* loop) : loop_(loop) {
    // Connect to the Scenic service.
    auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
    scenic_ = context->svc()->Connect<fuchsia::ui::scenic::Scenic>();
    scenic_.set_error_handler([this](zx_status_t status) {
      FX_LOGS(ERROR) << "Lost connection to Scenic service.";
      encountered_error_ = true;
      loop_->Quit();
    });
  }

  bool encountered_error() const { return encountered_error_; }

  void TakeScreenshot() {
    FX_LOGS(INFO) << "start TakeScreenshot";
    // If we wait for a call back from GetDisplayInfo, we are guaranteed that
    // the GFX system is initialized, which is a prerequisite for taking a
    // screenshot. TODO(fxbug.dev/23901): Remove call to GetDisplayInfo once bug done.
    scenic_->GetDisplayInfo(
        [this](fuchsia::ui::gfx::DisplayInfo /*unused*/) { TakeScreenshotInternal(); });
  }

 private:
  void TakeScreenshotInternal() {
    FX_LOGS(INFO) << "start TakeScreenshotInternal";
    scenic_->TakeScreenshot([this](fuchsia::ui::scenic::ScreenshotData screenshot, bool status) {
      FX_LOGS(INFO) << "start pixel capture";
      std::vector<uint8_t> imgdata;
      if (!status || !fsl::VectorFromVmo(screenshot.data, &imgdata)) {
        FX_LOGS(ERROR) << "TakeScreenshot failed";
        encountered_error_ = true;
        loop_->Quit();
        return;
      }

      std::cout << "P6\n";
      std::cout << screenshot.info.width << "\n";
      std::cout << screenshot.info.height << "\n";
      std::cout << 255 << "\n";

      FX_LOGS(INFO) << "capturing pixels";
      const uint8_t* pchannel = &imgdata[0];
      for (uint32_t pixel = 0; pixel < screenshot.info.width * screenshot.info.height; pixel++) {
        uint8_t rgb[] = {pchannel[2], pchannel[1], pchannel[0]};
        std::cout.write(reinterpret_cast<const char*>(rgb), 3);
        pchannel += 4;
      }
      loop_->Quit();
    });
  }
  async::Loop* loop_;
  bool encountered_error_ = false;
  fuchsia::ui::scenic::ScenicPtr scenic_;
};

int main(int argc, const char** argv) {
  FX_LOGS(INFO) << "starting screen capture";
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  if (!command_line.positional_args().empty()) {
    FX_LOGS(ERROR) << "Usage: screencap\n"
                   << "Takes a screenshot in PPM format and writes it "
                   << "to stdout.\n"
                   << "To write to a file, redirect stdout, e.g.: "
                   << "screencap > \"${DST}\"";
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  ScreenshotTaker taker(&loop);
  taker.TakeScreenshot();
  loop.Run();

  return taker.encountered_error() ? EXIT_FAILURE : EXIT_SUCCESS;
}
