// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/processargs.h>

#include "src/developer/debug/debug_agent/debug_agent.h"
#include "src/developer/debug/debug_agent/remote_api_adapter.h"
#include "src/developer/debug/debug_agent/zircon_system_interface.h"
#include "src/developer/debug/shared/platform_message_loop.h"

int main(int argc, const char* argv[]) {
  debug::PlatformMessageLoop message_loop;
  std::string init_error_message;
  if (!message_loop.Init(&init_error_message)) {
    LOGS(Error) << init_error_message;
    return 1;
  }

  // The scope ensures the objects are destroyed before calling Cleanup on the MessageLoop.
  {
    debug_agent::DebugAgent debug_agent(std::make_unique<debug_agent::ZirconSystemInterface>());

    // Must correspond to main_launcher.cc.
    zx::socket socket(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
    FX_CHECK(socket.is_valid());

    debug::BufferedZxSocket buffer(std::move(socket));

    // Route data from the zx::socket -> BufferedZxSocket -> RemoteAPIAdapter -> DebugAgent.
    debug_agent::RemoteAPIAdapter adapter(&debug_agent, &buffer.stream());
    buffer.set_data_available_callback([&adapter]() { adapter.OnStreamReadable(); });

    // Exit the message loop on error.
    buffer.set_error_callback([&debug_agent, &message_loop]() {
      DEBUG_LOG(Agent) << "Remote socket connection lost";
      message_loop.QuitNow();
      debug_agent.Disconnect();
    });

    // Connect the buffer into the agent.
    debug_agent.Connect(&buffer.stream());
    if (!buffer.Start()) {
      LOGS(Error) << "Fail to connect to the FIDL socket";
    } else {
      LOGS(Info) << "Remote client connected to debug_agent";
      message_loop.Run();
    }
  }

  message_loop.Cleanup();

  // It's very useful to have a simple message that informs the debug_agent exited successfully.
  LOGS(Info) << "See you, Space Cowboy...";
  return 0;
}
