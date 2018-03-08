// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/cpp/loop.h>
#include <async/default.h>
#include <launchpad/launchpad.h>
#include <zircon/processargs.h>
#include <zx/process.h>

#include "garnet/examples/fidl2/services/echo2.fidl.cc.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/svc/cpp/services.h"

namespace echo2 {

class EchoClientApp {
 public:
  EchoClientApp()
      : context_(app::ApplicationContext::CreateFromStartupInfo()) {}

  echo2::EchoPtr& echo() { return echo_; }

  void Start(std::string server_url, std::string msg) {
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = server_url;
    launch_info->service_request = echo_provider_.NewRequest();
    context_->launcher()->CreateApplication(std::move(launch_info),
                                            controller_.NewRequest());

    echo_provider_.ConnectToService(echo_.NewRequest().TakeChannel(),
                                    "echo2.Echo");
  }

 private:
  EchoClientApp(const EchoClientApp&) = delete;
  EchoClientApp& operator=(const EchoClientApp&) = delete;

  std::unique_ptr<app::ApplicationContext> context_;
  zx::process server_;
  app::Services echo_provider_;
  app::ApplicationControllerPtr controller_;
  echo2::EchoPtr echo_;
};

}  // namespace echo2

int main(int argc, const char** argv) {
  std::string server_url = "echo2_server_cpp";
  std::string msg = "hello world";

  for (int i = 1; i < argc - 1; ++i) {
    if (!strcmp("--server", argv[i])) {
      server_url = argv[++i];
    } else if (!strcmp("-m", argv[i])) {
      msg = argv[++i];
    }
  }

  async_loop_config_t config = {
      // The FIDL support lib requires async_get_default() to return non-null.
      .make_default_for_current_thread = true,
  };
  async::Loop loop(&config);

  echo2::EchoClientApp app;
  app.Start(server_url, msg);

  app.echo()->EchoString(msg, [](fidl::StringPtr value) {
    printf("***** Response: %s\n", value->data());
  });

  return app.echo().WaitForResponse();
}
