// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/examples/routing/echo/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/string.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, const char* argv[], char* envp[]) {
  syslog::SetTags({"echo_client"});
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  // Connect to the fidl.examples.routing.Echo protocol
  fidl::examples::routing::echo::EchoPtr echo_proxy;
  auto context = sys::ComponentContext::Create();
  context->svc()->Connect(echo_proxy.NewRequest());

  // Sets an error handler that will be called if an error causes the underlying
  // channel to be closed.
  echo_proxy.set_error_handler([&loop](zx_status_t status) {
    FX_SLOG(WARNING, "Echo protocol failed", KV("status", status));
    ZX_ASSERT(status == ZX_ERR_UNAVAILABLE);
    loop.Quit();
  });

  // The `echo` channel should be closed with an epitaph because routing failed (see
  // echo_realm.cml)
  //
  // The epitaph itself is just a zx_status_t. To get detailed information about why the routing
  // failed, you'll need to check the kernel debuglog.
  echo_proxy->EchoString("Hippos rule!", [&](fidl::StringPtr response) {
    FX_SLOG(INFO, "Server response", KV("response", response->c_str()));
    loop.Quit();
  });

  loop.Run();
  loop.ResetQuit();

  // Connect to the fidl.examples.routing.Echo2 protocol
  fidl::examples::routing::echo::EchoPtr echo2_proxy;
  auto request = echo2_proxy.NewRequest();
  context->svc()->Connect("fidl.examples.routing.echo.Echo2", request.TakeChannel());

  // Sets an error handler that will be called if an error causes the underlying
  // channel to be closed.
  echo2_proxy.set_error_handler([&loop](zx_status_t status) {
    FX_SLOG(WARNING, "Echo2 protocol failed", KV("status", status));
    ZX_ASSERT(status == ZX_ERR_PEER_CLOSED);
    loop.Quit();
  });

  // The `echo2` channel should be closed because routing succeeded but the runner failed to
  // start the component. The channel won't have an epitaph set; the runner closes the source
  // component's outgoing directory request handle and that causes the channel for the service
  // connection to be closed as well.
  echo2_proxy->EchoString("Hippos rule!", [&](fidl::StringPtr response) {
    FX_SLOG(INFO, "Server response", KV("response", response->c_str()));
    loop.Quit();
  });

  loop.Run();
  return 0;
}
