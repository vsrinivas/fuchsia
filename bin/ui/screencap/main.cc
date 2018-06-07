// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <memory>

#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

#include "garnet/lib/ui/gfx/gfx_system.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "garnet/lib/ui/scenic/system.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/fsl/vmo/vector.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  const auto& positional_args = command_line.positional_args();
  if (!positional_args.empty()) {
    FXL_LOG(ERROR) << "Usage: screencap\n"
                   << "Takes a screenshot in PPM format and writes it "
                   << "to stdout.\n"
                   << "To write to a file, redirect stdout, e.g.: "
                   << "screencap > \"${DST}\"";
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  trace::TraceProvider trace_provider(loop.async());
  std::unique_ptr<fuchsia::sys::StartupContext> app_context(
      fuchsia::sys::StartupContext::CreateFromStartupInfo());
  // Connect to the SceneManager service.
  auto scenic =
      app_context->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
  scenic.set_error_handler([&loop] {
    FXL_LOG(ERROR) << "Lost connection to Scenic service.";
    loop.Quit();
  });
  scenic->TakeScreenshot([&loop](
                             fuchsia::ui::scenic::ScreenshotData screenshot,
                             bool status) {
    std::vector<uint8_t> imgdata;
    if (!status || !fsl::VectorFromVmo(screenshot.data, &imgdata)) {
      FXL_LOG(ERROR) << "TakeScreenshot failed";
      loop.Quit();
      return;
    }

    std::cout << "P6\n";
    std::cout << screenshot.info.width << "\n";
    std::cout << screenshot.info.height << "\n";
    std::cout << 255 << "\n";

    const uint8_t* pchannel = &imgdata[0];
    for (uint32_t pixel = 0;
         pixel < screenshot.info.width * screenshot.info.height; pixel++) {
      uint8_t rgb[] = {pchannel[2], pchannel[1], pchannel[0]};
      std::cout.write(reinterpret_cast<const char*>(rgb), 3);
      pchannel += 4;
    }
    loop.Quit();
  });
  loop.Run();

  return 0;
}
