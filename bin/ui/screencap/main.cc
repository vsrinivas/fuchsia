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

// These are the only values we want to track
const int kBlack = 0x000000;
const int kWhite = 0xeeeeee;
const int kGreen = 0x4dac26;
const int kRed = 0xd01c8b;

const int kMinExpectedPixels = 950000;  // Typical value is ~1.5M
const int kMinPixelsForReport = 50000;

int main(int argc, const char** argv) {
  int return_code = 0;
  bool output_screen = true;
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) return 1;

  const auto& positional_args = command_line.positional_args();
  if (!positional_args.empty()) {
    if (positional_args.size() == 1 &&
        positional_args[0].compare("-histogram") == 0) {
      output_screen = false;
    } else {
      FXL_LOG(ERROR) << "Usage: screencap\n"
                     << "Takes a screenshot in PPM format and writes it "
                     << "to stdout.\n"
                     << "To write to a file, redirect stdout, e.g.: "
                     << "screencap > \"${DST}\"";
      return 1;
    }
  }

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  trace::TraceProvider trace_provider(loop.async());
  std::unique_ptr<fuchsia::sys::StartupContext> app_context(
      fuchsia::sys::StartupContext::CreateFromStartupInfo());
  // Connect to the SceneManager service.
  auto scenic =
      app_context->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
  scenic.set_error_handler([&loop, &return_code] {
    FXL_LOG(ERROR) << "Lost connection to Scenic service.";
    return_code = 1;
    loop.Quit();
  });
  scenic->TakeScreenshot([&loop, &return_code, output_screen](
                             fuchsia::ui::scenic::ScreenshotData screenshot,
                             bool status) {
    std::vector<uint8_t> imgdata;
    if (!status || !fsl::VectorFromVmo(screenshot.data, &imgdata)) {
      FXL_LOG(ERROR) << "TakeScreenshot failed";
      return_code = 1;
      loop.Quit();
      return;
    }

    std::map<int, int> histogram;
    if (output_screen) {
      std::cout << "P6\n";
      std::cout << screenshot.info.width << "\n";
      std::cout << screenshot.info.height << "\n";
      std::cout << 255 << "\n";
    }

    const uint8_t* pchannel = &imgdata[0];
    for (uint32_t pixel = 0;
         pixel < screenshot.info.width * screenshot.info.height; pixel++) {
      uint8_t rgb[] = {pchannel[2], pchannel[1], pchannel[0]};
      if (output_screen) {
        std::cout.write(reinterpret_cast<const char*>(rgb), 3);
      } else {
        int rgb_value = (rgb[0] << 16) + (rgb[1] << 8) + rgb[2];
        if (histogram.find(rgb_value) != histogram.end()) {
          histogram[rgb_value] = histogram[rgb_value] + 1;
        } else {
          histogram[rgb_value] = 1;
        }
      }
      pchannel += 4;
    }
    if (!output_screen) {
      // For success, there should be at least 1M green or red pixels combined
      // The typical number is > 1.5M
      if (histogram[kGreen] + histogram[kRed] > kMinExpectedPixels) {
        printf("success\n");
      } else {
        printf("failure\n");
        printf("black: %d, white: %d, green: %d, red: %d\n", histogram[kBlack],
               histogram[kWhite], histogram[kGreen], histogram[kRed]);
        // To help debug failures, if the majority of values aren't already
        // expected, output the values that were over a threshold count.
        if (histogram[kBlack] + histogram[kWhite] + histogram[kGreen] +
                histogram[kRed] <
            kMinExpectedPixels) {
          const int MIN_REPORT_THRESHOLD = kMinPixelsForReport;
          std::map<int, int>::iterator it;
          for (const auto& pair : histogram) {
            if (pair.second > MIN_REPORT_THRESHOLD) {
              printf("Pixel 0x%06x occurred %d times\n", pair.first,
                     pair.second);
            }
          }
        }
        return_code = 30;
      }
    }
    loop.Quit();
  });
  loop.Run();

  return return_code;
}
