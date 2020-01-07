// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/virtualconsole/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/watcher.h>
#include <lib/zircon-internal/paths.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

static zx_status_t connect_to_service(const char* service, zx_handle_t* channel) {
  zx_handle_t channel_local, channel_remote;
  zx_status_t status = zx_channel_create(0, &channel_local, &channel_remote);
  if (status != ZX_OK) {
    fprintf(stderr, "run-vc: failed to create channel: %s\n", zx_status_get_string(status));
    return false;
  }

  status = fdio_service_connect(service, channel_remote);
  if (status != ZX_OK) {
    zx_handle_close(channel_local);
    fprintf(stderr, "run-vc: failed to connect to service: %s\n", zx_status_get_string(status));
    return false;
  }

  *channel = channel_local;
  return true;
}

int main(int argc, const char** argv) {
  zx_handle_t session_manager = ZX_HANDLE_INVALID;
  if (!connect_to_service("/svc/fuchsia.virtualconsole.SessionManager", &session_manager)) {
    return -1;
  }

  zx_handle_t session, session_remote;
  if (zx_channel_create(0, &session, &session_remote) != ZX_OK) {
    return -1;
  }

  zx_status_t remote_status = ZX_OK;
  zx_status_t status = fuchsia_virtualconsole_SessionManagerCreateSession(
      session_manager, session_remote, &remote_status);
  if (status != ZX_OK || remote_status != ZX_OK) {
    fprintf(stderr, "run-vc: failed to create session: local: %s remote: %s\n",
            zx_status_get_string(status), zx_status_get_string(remote_status));
    return -1;
  }
  if (session == ZX_HANDLE_INVALID) {
    fprintf(stderr, "run-vc: Received invalid handle from session manager!\n");
    return -1;
  }

  // start shell if no arguments
  if (argc == 1) {
    argv[0] = ZX_SHELL_DEFAULT;
  } else {
    argv++;
  }

  const char* pname = strrchr(argv[0], '/');
  if (pname == NULL) {
    pname = argv[0];
  } else {
    pname++;
  }

  uint32_t flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO;
  uint32_t type = PA_HND(PA_FD, FDIO_FLAG_USE_FOR_STDIO);

  fdio_spawn_action_t actions[2] = {
      {.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = pname}},
      {.action = FDIO_SPAWN_ACTION_ADD_HANDLE, .h = {.id = type, .handle = session}},
  };

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  status = fdio_spawn_etc(ZX_HANDLE_INVALID, flags, argv[0], argv, NULL, countof(actions), actions,
                          NULL, err_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "error %d (%s) launching: %s\n", status, zx_status_get_string(status), err_msg);
    return -1;
  }
  return 0;
}
