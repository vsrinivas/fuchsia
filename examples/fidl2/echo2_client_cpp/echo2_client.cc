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

  zx_status_t Start(std::string server_url, std::string msg) {
    zx_status_t status = LaunchServer(std::move(server_url));
    if (status != ZX_OK)
      return status;

    echo_provider_.ConnectToService(echo_.NewRequest().TakeChannel(),
                                    "echo2.Echo");
    printf("***** Connected\n");
    return ZX_OK;
  }

 private:
  EchoClientApp(const EchoClientApp&) = delete;
  EchoClientApp& operator=(const EchoClientApp&) = delete;

  // This function is a workaround for not being able to use ApplicationLauncher
  // yet.  We can't use ApplicationLauncher at the moment because it speaks
  // FIDL1 rather than FIDL2.
  zx_status_t LaunchServer(std::string server_url) {
    constexpr int argc = 1;
    const char* argv[argc] = {
        server_url.c_str(),
    };
    launchpad_t* lp = nullptr;
    launchpad_create(zx_job_default(), argv[0], &lp);
    launchpad_load_from_file(lp, argv[0]);
    launchpad_set_args(lp, argc, argv);
    launchpad_clone(lp, LP_CLONE_ALL);
    launchpad_add_handle(lp, echo_provider_.NewRequest().release(),
                         PA_SERVICE_REQUEST);
    const char* err = nullptr;
    zx_status_t status =
        launchpad_go(lp, server_.reset_and_get_address(), &err);
    if (status < 0)
      fprintf(stderr, "launchpad failed: %s: %d\n", err, status);
    return status;
  }

  std::unique_ptr<app::ApplicationContext> context_;
  zx::process server_;
  app::Services echo_provider_;
  app::ApplicationControllerPtr controller_;
  echo2::EchoPtr echo_;
};

}  // namespace echo2

int main(int argc, const char** argv) {
  std::string server_url = "/pkgfs/packages/echo2_server_cpp/0/bin/app";
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
  if (app.Start(server_url, msg) != ZX_OK)
    return -1;

  app.echo()->EchoString(msg, [](fidl::StringPtr value) {
    printf("***** Response: %s\n", value->data());
  });

  printf("***** Waiting for response\n");
  if (app.echo().WaitForResponse() != ZX_OK)
    return -1;

  printf("***** Exiting\n");
  return 0;
}
