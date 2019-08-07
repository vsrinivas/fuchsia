// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <stdio.h>

int main(int argc, char** argv) {
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_status_t status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[1],
                                      (const char* const*)argv + 1, NULL, 0, NULL, NULL, err_msg);

  if (status != ZX_OK)
    fprintf(stderr, "error: fdio_spawn: %s\n", err_msg);

  return status;
}
