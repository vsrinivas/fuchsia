// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "yuv_view.h"

#include <lib/async-loop/cpp/loop.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <lib/ui/base_view/cpp/view_provider_component.h>
#include <trace-provider/provider.h>

// fx shell "killall scenic; killall basemgr; killall root_presenter;
// killall present_view"
//
// fx shell "present_view yuv_to_image_pipe --NV12"
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    printf("fxl::SetLogSettingsFromCommandLine() failed\n");
    exit(-1);
  }

  static struct OptionEntry {
    std::string option;
    fuchsia::images::PixelFormat pixel_format;
  } table[] = {
      {"NV12", fuchsia::images::PixelFormat::NV12},
      {"YUY2", fuchsia::images::PixelFormat::YUY2},
      {"BGRA_8", fuchsia::images::PixelFormat::BGRA_8},
      {"YV12", fuchsia::images::PixelFormat::YV12},
  };

  fuchsia::images::PixelFormat pixel_format;
  uint32_t option_count = 0;
  for (const OptionEntry& option_entry : table) {
    if (command_line.HasOption(option_entry.option)) {
      if (option_count != 0) {
        printf("Too many PixelFormat options.\n");
        exit(-1);
      }
      pixel_format = option_entry.pixel_format;
      option_count++;
    }
  }
  if (option_count == 0) {
    printf("Missing format flag such as --NV12\n");
    exit(-1);
  }

  scenic::ViewProviderComponent component(
      [pixel_format](scenic::ViewContext view_context) {
        return std::make_unique<YuvView>(std::move(view_context), pixel_format);
      },
      &loop);

  loop.Run();
  return 0;
}
