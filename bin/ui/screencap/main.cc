// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <memory>

#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

#include "garnet/lib/ui/gfx/gfx_system.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "garnet/lib/ui/scenic/system.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fsl/vmo/vector.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  const auto& positional_args = command_line.positional_args();
  if (positional_args.empty()) {
    FXL_LOG(ERROR) << "screencap requires a path for where to save "
                   << "the screenshot.";
    return 1;
  }
  const std::string filename = positional_args[0];

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  trace::TraceProvider trace_provider(loop.async());
  std::unique_ptr<component::ApplicationContext> app_context(
      component::ApplicationContext::CreateFromStartupInfo());
  // Connect to the SceneManager service.
  auto scenic =
      app_context->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
  scenic.set_error_handler([&loop] {
    FXL_LOG(ERROR) << "Lost connection to Scenic service.";
    loop.Quit();
  });
  scenic->TakeScreenshot([&loop, &filename](
                             fuchsia::ui::scenic::ScreenshotData screenshot,
                             bool status) {
    std::vector<uint8_t> imgdata;
    if (!status || !fsl::VectorFromVmo(screenshot.data, &imgdata)) {
      FXL_LOG(ERROR) << "TakeScreenshot failed";
      loop.Quit();
      return;
    }

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
      FXL_LOG(ERROR) << "Could not open file to write screenshot: " << filename;
      loop.Quit();
      return;
    }

    file << "P6\n";
    file << screenshot.info.width << "\n";
    file << screenshot.info.height << "\n";
    file << 255 << "\n";

    const uint8_t* pchannel = &imgdata[0];
    for (uint32_t pixel = 0;
         pixel < screenshot.info.width * screenshot.info.height; pixel++) {
      uint8_t rgb[] = {pchannel[2], pchannel[1], pchannel[0]};
      file.write(reinterpret_cast<const char*>(rgb), 3);
      pchannel += 4;
    }
    file.close();
    loop.Quit();
  });
  loop.Run();

  return 0;
}
