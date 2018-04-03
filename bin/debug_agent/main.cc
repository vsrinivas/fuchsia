// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fdio/io.h>
#include <stdio.h>
#include <launchpad/launchpad.h>
#include <zx/process.h>

#include "garnet/bin/debug_agent/debug_agent.h"
#include "garnet/bin/debug_agent/exception_handler.h"
#include "garnet/bin/debug_agent/remote_api_adapter.h"

// Currently this is just a manual test that sets up the exception handler and
// spawns a process for
// it to listen to.

int main(int argc, char* argv[]) {
  zx::socket client_socket, router_socket;
  if (zx::socket::create(ZX_SOCKET_STREAM, &client_socket, &router_socket) !=
      ZX_OK)
    fprintf(stderr, "Can't create socket.\n");

  // Data flows from ExceptionHandler -> RemoteAPIAdapter -> DebugAgent;
  debug_agent::ExceptionHandler handler;
  debug_agent::DebugAgent agent(&handler);
  debug_agent::RemoteAPIAdapter adapter(&agent, &handler.socket_buffer());
  handler.set_read_watcher(&adapter);
  handler.set_process_watcher(&agent);

  if (!handler.Start(std::move(router_socket))) {
    fprintf(stderr, "Can't start thread.\n");
    return 1;
  }

  // TODO(brettw) stuff goes here.

  fprintf(stderr, "Sleeping...\n");
  zx_nanosleep(zx_deadline_after(ZX_MSEC(2000)));
  fprintf(stderr, "Done sleeping, exiting.\n");

  // Ensure the ExceptionHandler doesn't try to use these pointers (the
  // ExceptionHandler will be destroyed last).
  handler.Shutdown();
  handler.set_read_watcher(nullptr);
  handler.set_process_watcher(nullptr);

  return 0;
}
