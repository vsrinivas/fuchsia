// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fdio/io.h>
#include <stdio.h>
#include <launchpad/launchpad.h>
#include <zx/process.h>

#include "garnet/bin/debug_agent/exception_handler.h"

// Currently this is just a manual test that sets up the exception handler and
// spawns a process for
// it to listen to.

zx::process Launch(const char* path) {
  launchpad_t* lp;
  zx_status_t status = launchpad_create(0, path, &lp);
  if (status != ZX_OK)
    return zx::process();

  status = launchpad_load_from_file(lp, path);
  if (status != ZX_OK)
    return zx::process();

  // Command line arguments.
  const char* argv[1] = {path};
  status = launchpad_set_args(lp, 1, argv);
  if (status != ZX_OK)
    return zx::process();

  // Transfer STDIO handles.
  status = launchpad_transfer_fd(lp, 1, FDIO_FLAG_USE_FOR_STDIO | 0);
  if (status != ZX_OK)
    return zx::process();

  launchpad_clone(
      lp, LP_CLONE_FDIO_NAMESPACE | LP_CLONE_ENVIRON | LP_CLONE_DEFAULT_JOB);
  if (status != ZX_OK)
    return zx::process();

  zx_handle_t child;
  status = launchpad_go(lp, &child, NULL);
  if (status != ZX_OK)
    return zx::process();

  return zx::process(child);
}

int main(int argc, char* argv[]) {
  zx::socket client_socket, router_socket;
  if (zx::socket::create(ZX_SOCKET_STREAM, &client_socket, &router_socket) !=
      ZX_OK)
    fprintf(stderr, "Can't create socket.\n");

  ExceptionHandler handler;
  if (!handler.Start(std::move(router_socket))) {
    fprintf(stderr, "Can't start thread.\n");
    return 1;
  }

  zx::process process = Launch("/boot/bin/ps");
  if (!handler.Attach(std::move(process)))
    fprintf(stderr, "Couldn't attach.\n");

  // TEST
  char hello[] = "hello";
  size_t written = 0;
  client_socket.write(0, hello, 5, &written);
  fprintf(stderr, "Wrote %d\n", (int)written);

  fprintf(stderr, "Sleeping...\n");
  zx_nanosleep(zx_deadline_after(ZX_MSEC(1000)));
  fprintf(stderr, "Done sleeping, exiting.\n");
  return 0;
}
