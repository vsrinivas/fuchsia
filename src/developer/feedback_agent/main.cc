// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/logger.h>
#include <stdlib.h>
#include <zircon/errors.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

fidl::InterfaceRequestHandler<fuchsia::feedback::DataProvider>
SpawnNewDataProvider() {
  return [](fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request) {
    // We spawn a new process to which we forward the channel of the incoming
    // request so it can handle it.
    //
    // Note that today we do not keep track of the spawned process.
    fdio_spawn_action_t actions = {};
    actions.action = FDIO_SPAWN_ACTION_ADD_HANDLE;
    actions.h.id = PA_HND(PA_USER0, 0);
    actions.h.handle = request.TakeChannel().release();

    const char* args[] = {
        "/pkg/bin/data_provider",
        nullptr,
    };
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH] = {};
    const zx_status_t spawn_status =
        fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, args[0], args,
                       nullptr, 1, &actions, nullptr, err_msg);
    if (spawn_status != ZX_OK) {
      FX_PLOGS(ERROR, spawn_status)
          << "Failed to spawn data provider to handle incoming request: "
          << err_msg;
    }
  };
}

int main(int argc, const char** argv) {
  syslog::InitLogger({"feedback"});

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = sys::ComponentContext::Create();
  // We spawn a new process capable of handling fuchsia.feedback.DataProvider
  // requests on every incoming request. This has the advantage of tying each
  // request to a different process that can be cleaned up once it is done or
  // after a timeout and take care of dangling threads for instance, cf. CF-756.
  context->outgoing()->AddPublicService(SpawnNewDataProvider());

  loop.Run();

  return EXIT_SUCCESS;
}
