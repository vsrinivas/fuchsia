// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"
#include "lib/svc/cpp/services.h"

#include "garnet/examples/fidl/services/echo.fidl.h"

namespace echo {

class ResponsePrinter {
 public:
  void Run(const f1dl::StringPtr& value) const {
    printf("***** Response: %s\n", value.get().c_str());

    fsl::MessageLoop::GetCurrent()->QuitNow();
  }
};

class EchoClientApp {
 public:
  EchoClientApp()
      : context_(component::ApplicationContext::CreateFromStartupInfo()) {
    FXL_DCHECK(context_);
  }

  bool Start(std::string server_url, std::string msg) {
    auto launch_info = component::ApplicationLaunchInfo::New();
    launch_info->url = server_url;
    launch_info->directory_request = echo_provider_.NewRequest();

    context_->launcher()->CreateApplication(std::move(launch_info),
                                            controller_.NewRequest());

    echo_provider_.ConnectToService(echo_.NewRequest());
    FXL_DCHECK(echo_);

    echo_->EchoString(msg, [this](f1dl::StringPtr value) {
      ResponsePrinter printer;
      printer.Run(std::move(value));
    });
    return true;
  }

 private:
  std::unique_ptr<component::ApplicationContext> context_;
  component::Services echo_provider_;
  component::ApplicationControllerPtr controller_;
  echo::EchoPtr echo_;
};

}  // namespace echo

int main(int argc, const char** argv) {
  std::string server_url = "echo_server_cpp";
  std::string msg = "hello world";

  for (int i = 1; i < argc - 1; ++i) {
    if (!strcmp("--server", argv[i])) {
      server_url = argv[++i];
    } else if (!strcmp("-m", argv[i])) {
      msg = argv[++i];
    }
  }
  fsl::MessageLoop loop;

  echo::EchoClientApp app;
  if (app.Start(server_url, msg))
    loop.Run();
  return 0;
}
