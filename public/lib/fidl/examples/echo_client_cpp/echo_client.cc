// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/ftl/macros.h"

#include "lib/fidl/examples/services/echo.fidl.h"

namespace echo {

class ResponsePrinter {
 public:
  void Run(const fidl::String& value) const {
    printf("***** Response: %s\n", value.get().c_str());

    mtl::MessageLoop::GetCurrent()->QuitNow();
  }
};

class EchoClientApp {
 public:
  EchoClientApp()
    : context_(app::ApplicationContext::CreateFromStartupInfo()) {
    FTL_DCHECK(context_);
  }

  bool Start(std::string server_url) {
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = server_url;
    launch_info->services = echo_provider_.NewRequest();

    context_->launcher()->CreateApplication(std::move(launch_info),
                                            controller_.NewRequest());

    app::ConnectToService(echo_provider_.get(), echo_.NewRequest());
    FTL_DCHECK(echo_);

    echo_->EchoString("hello world",
                      [this](fidl::String value) {
                        ResponsePrinter printer;
                        printer.Run(std::move(value));
                      });
    return true;
  }

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  app::ServiceProviderPtr echo_provider_;
  app::ApplicationControllerPtr controller_;
  echo::EchoPtr echo_;
};

}  // namespace echo

int main(int argc, const char** argv) {
  std::string server_url = "file:///system/apps/echo_server_cpp";
  if (argc == 2) {
    server_url = argv[1];
  }
  mtl::MessageLoop loop;

  echo::EchoClientApp app;
  if (app.Start(server_url))
    loop.Run();
  return 0;
}
