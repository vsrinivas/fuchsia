// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This simple program listens on the fuchsia::debugger::DebugAgent protocol,
// and launch a debug_agent when there's a connect request. The debug_agent
// launched expects a numbered handle at PA_HND(PA_USER0, 0), which should
// point to a zx::socket object.

#include <fuchsia/debugger/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>

namespace {

class DebugAgentLauncher : public fuchsia::debugger::DebugAgent {
 public:
  // Launch debug_agent on connect, passing the socket as a numbered handle.
  void Connect(zx::socket socket, ConnectCallback callback) override {
    FX_LOGS(DEBUG) << "Spawning debug_agent...";
    const char* path = "/pkg/bin/debug_agent";
    const char* argv[] = {path, "--channel-mode", nullptr};
    fdio_spawn_action_t action = {
        .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
        .h =
            {
                // Must correspond to main.cc.
                .id = PA_HND(PA_USER0, 0),
                .handle = socket.release(),
            },
    };
    zx::process process;
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH] = {};
    zx_status_t status =
        fdio_spawn_etc(zx_job_default(), FDIO_SPAWN_CLONE_ALL, path, argv,
                       /*environ=*/nullptr, 1, &action, process.reset_and_get_address(), err_msg);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to launch debug_agent: " << err_msg;
    }
    callback(status);
  }
};

}  // namespace

int main() {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  syslog::SetLogSettings(syslog::LogSettings{});

  DebugAgentLauncher launcher;
  fidl::Binding<fuchsia::debugger::DebugAgent> binding(&launcher);
  fidl::InterfaceRequestHandler<fuchsia::debugger::DebugAgent> handler =
      [&](fidl::InterfaceRequest<fuchsia::debugger::DebugAgent> request) {
        binding.Bind(std::move(request));
      };
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(std::move(handler));

  FX_LOGS(INFO) << "Start listening on FIDL fuchsia::debugger::DebugAgent.";
  return loop.Run();
}
