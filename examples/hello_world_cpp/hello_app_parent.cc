// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <memory>
#include <string>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "apps/modular/examples/hello_world_cpp/hello.fidl.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"
#include "lib/fsl/tasks/message_loop.h"

using examples::HelloPtr;

namespace {

class HelloAppParent {
 public:
  explicit HelloAppParent(fxl::CommandLine& command_line)
      : context_(app::ApplicationContext::CreateFromStartupInfo()) {
    auto launch_info = app::ApplicationLaunchInfo::New();
    const std::vector<std::string>& args = command_line.positional_args();
    if (args.empty()) {
      launch_info->url = "file:///system/apps/hello_app_child";
    } else {
      launch_info->url = args[0];
      for (size_t i = 1; i < args.size(); ++i) {
        launch_info->arguments.push_back(args[i]);
      }
    }
    launch_info->services = child_services_.NewRequest();
    context_->launcher()->CreateApplication(std::move(launch_info),
                                            child_.NewRequest());

    ConnectToService(child_services_.get(), hello_.NewRequest());

    DoIt("hello");
    DoIt("goodbye");
  }

 private:
  void DoIt(const std::string& request) {
    hello_->Say(request, [request](const fidl::String& response) {
      printf("%s --> %s\n", request.c_str(), response.get().c_str());
    });
  }

  std::unique_ptr<app::ApplicationContext> context_;

  app::ApplicationControllerPtr child_;
  app::ServiceProviderPtr child_services_;
  HelloPtr hello_;

  FXL_DISALLOW_COPY_AND_ASSIGN(HelloAppParent);
};

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  fsl::MessageLoop loop;
  HelloAppParent app(command_line);
  loop.Run();
  return 0;
}
