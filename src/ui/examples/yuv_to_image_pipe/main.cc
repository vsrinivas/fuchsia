// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <trace-provider/provider.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/ui/base_view/view_provider_component.h"
#include "src/ui/examples/yuv_to_image_pipe/yuv_cyclic_view.h"
#include "src/ui/examples/yuv_to_image_pipe/yuv_input_view.h"

// fx shell "killall scenic; killall basemgr; killall root_presenter;
// killall present_view"
//
// fx shell "present_view yuv_to_image_pipe --NV12"
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    printf("fxl::SetLogSettingsFromCommandLine() failed\n");
    exit(-1);
  }

  static struct OptionEntry {
    std::string option;
    fuchsia::sysmem::PixelFormatType pixel_format;
  } table[] = {
      {"NV12", fuchsia::sysmem::PixelFormatType::NV12},
      {"BGRA32", fuchsia::sysmem::PixelFormatType::BGRA32},
      {"R8G8B8A8", fuchsia::sysmem::PixelFormatType::R8G8B8A8},
      {"I420", fuchsia::sysmem::PixelFormatType::I420},
  };

  fuchsia::sysmem::PixelFormatType pixel_format;
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

  scenic::ViewFactory factory;
  if (command_line.HasOption("input_driven")) {
    factory = [pixel_format](scenic::ViewContext view_context) {
      return std::make_unique<yuv_to_image_pipe::YuvInputView>(std::move(view_context),
                                                               pixel_format);
    };
  } else {
    factory = [pixel_format](scenic::ViewContext view_context) {
      return std::make_unique<yuv_to_image_pipe::YuvCyclicView>(std::move(view_context),
                                                                pixel_format);
    };
  }
  scenic::ViewProviderComponent component(std::move(factory), &loop);
  loop.Run();
  return 0;
}
