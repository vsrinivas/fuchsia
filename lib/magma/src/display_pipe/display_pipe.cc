// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h> // for close

#include <zircon/syscalls.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "magma.h"
#include "magma_util/macros.h"
#include "magma_util/platform/zircon/zircon_platform_ioctl.h"

#include "display_provider_impl.h"
#include "magma_connection.h"

namespace display_pipe {

class App {
 public:
  App() : context_(app::ApplicationContext::CreateFromStartupInfo()) {
    context_->outgoing_services()->AddService<DisplayProvider>(
        [this](fidl::InterfaceRequest<DisplayProvider> request) {
          display_provider_.AddBinding(std::move(request));
        });
  }

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  DisplayProviderImpl display_provider_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace display_pipe

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  FXL_DLOG(INFO) << "display_pipe started";
  fsl::MessageLoop loop;
  display_pipe::App app;
  loop.Run();
  return 0;
}
