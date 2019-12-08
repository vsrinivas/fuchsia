// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netsvc.h"

#include <stdio.h>

#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/debuglog.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

static void run_program(const char* progname, const char** argv, zx_handle_t h) {
  zx::debuglog logger;
  zx::debuglog::create(zx::resource(), 0, &logger);

  fdio_spawn_action_t actions[] = {
      {.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = progname}},
      {.action = FDIO_SPAWN_ACTION_ADD_HANDLE,
       .h = {.id = PA_HND(PA_FD, 0 | FDIO_FLAG_USE_FOR_STDIO), .handle = logger.release()}},
      {.action = FDIO_SPAWN_ACTION_ADD_HANDLE, .h = {.id = PA_HND(PA_USER0, 0), .handle = h}},
  };

  size_t action_count = (h == ZX_HANDLE_INVALID) ? 2 : 3;
  uint32_t flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];

  zx_status_t status = fdio_spawn_etc(ZX_HANDLE_INVALID, flags, argv[0], argv, NULL, action_count,
                                      actions, NULL, err_msg);

  if (status != ZX_OK) {
    printf("netsvc: cannot launch %s: %d: %s\n", argv[0], status, err_msg);
  }
}

void netboot_run_cmd(const char* cmd) {
  const char* argv[] = {"/boot/bin/sh", "-c", cmd, NULL};
  printf("net cmd: %s\n", cmd);
  run_program("net:sh", argv, ZX_HANDLE_INVALID);
}
