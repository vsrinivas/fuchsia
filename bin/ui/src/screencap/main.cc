// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>
#include <memory>

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "apps/mozart/lib/skia/skia_vmo_image.h"
#include "apps/mozart/services/buffers/buffer.fidl.h"
#include "apps/mozart/services/composition/compositor.fidl.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/log_settings_command_line.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/skia/include/core/SkEncodedImageFormat.h"

int main(int argc, char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  const auto& positional_args = command_line.positional_args();
  if (positional_args.empty()) {
    FTL_LOG(ERROR) << "screencap requires a path for where to save "
                      "the screenshot.";
    return 1;
  }

  int renderer_index = 0;
  if (command_line.HasOption("renderer")) {
    std::string index;
    if (command_line.GetOptionValue("renderer", &index)) {
      renderer_index = std::stoi(index);
    }
  }

  mtl::MessageLoop loop;

  FTL_LOG(INFO) << "Capturing renderer " << renderer_index << " to "
                << positional_args[0];

  auto application_context_ = app::ApplicationContext::CreateFromStartupInfo();
  app::ServiceProviderPtr services;
  fidl::InterfacePtr<mozart::Compositor> compositor =
      application_context_->ConnectToEnvironmentService<mozart::Compositor>();
  compositor->TakeScreenshot(
      renderer_index, [filename = positional_args[0]](mozart::ImagePtr image) {
        do {
          if (!image) {
            FTL_LOG(ERROR) << "Nothing captured";
            break;
          }

          FTL_LOG(INFO) << "Screenshot taken " << image->size->width << " x "
                        << image->size->height;
          mozart::BufferConsumer consumer;
          std::unique_ptr<mozart::BufferFence> fence;
          sk_sp<SkImage> sk_image =
              MakeSkImage(std::move(image), &consumer, &fence);
          if (!sk_image) {
            FTL_LOG(ERROR) << "Could not convert image";
            break;
          }

          // Quality set to 0 as it is not used for PNG
          SkData* data = sk_image->encode(SkEncodedImageFormat::kPNG, 0);

          int fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
          if (fd == -1) {
            FTL_LOG(ERROR) << "Could not open file " << filename << " : "
                           << errno;
            break;
          }
          write(fd, data->data(), data->size());
          close(fd);
          FTL_LOG(INFO) << "Screenshot saved at " << filename;
        } while (false);
        mtl::MessageLoop::GetCurrent()->PostQuitTask();
      });

  loop.Run();
  return 0;
}
