// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "yuv_view.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>
#include <lib/ui/view_framework/view_provider_app.h>
#include <trace-provider/provider.h>

// fx shell "killall scenic; killall device_runner; killall root_presenter;
// killall set_root_view"
//
// fx shell "set_root_view yuv_to_image_pipe --NV12"
int main(int argc, const char** argv) {
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

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());

  mozart::ViewProviderApp app([&loop,
                               pixel_format](mozart::ViewContext view_context) {
    return std::make_unique<YuvView>(&loop, view_context.startup_context,
                                     std::move(view_context.view_manager),
                                     std::move(view_context.view_owner_request),
                                     pixel_format);
  });

  loop.Run();
  return 0;
}
