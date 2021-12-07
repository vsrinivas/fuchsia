// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#define VDSO_FILE "/boot/kernel/vdso/test1"

int main(void) {
  int fd = open(VDSO_FILE, O_RDONLY);
  if (fd < 0) {
    printf("%s: %m\n", VDSO_FILE);
    return 1;
  }

  zx_handle_t vdso_vmo_noexec;
  zx_handle_t vdso_vmo;
  zx_status_t status = fdio_get_vmo_exact(fd, &vdso_vmo_noexec);
  close(fd);
  if (status != ZX_OK) {
    printf("fdio_get_vmo_exact(%d): %s\n", fd, zx_status_get_string(status));
    return status;
  }

  status = zx_vmo_replace_as_executable(vdso_vmo_noexec, ZX_HANDLE_INVALID, &vdso_vmo);
  if (status != ZX_OK) {
    printf("zx_vmo_replace_as_executable(%u, ZX_HANDLE_INVALID, *res): %s\n", vdso_vmo_noexec,
           zx_status_get_string(status));
    return status;
  }

  fdio_spawn_action_t actions[2];
  size_t action_count = 0;

  actions[action_count++] = (fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_SET_NAME,
      .name = {.data = "vdso-variant-helper"},
  };
  actions[action_count++] = (fdio_spawn_action_t){
      .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
      .h =
          {
              .id = PA_HND(PA_VMO_VDSO, 0),
              .handle = vdso_vmo,
          },
  };

  const char* root_dir = getenv("TEST_ROOT_DIR");
  if (root_dir == NULL) {
    root_dir = "";
  }
  static const char kHelperPath[] = "/bin/vdso-variant-helper";
  char path[strlen(root_dir) + strlen(kHelperPath) + 1];
  strcpy(path, root_dir);
  strcat(path, kHelperPath);

  const char* args[] = {
      "vdso-variant-helper",
      NULL,
  };
  zx_handle_t proc;
  char errmsg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, path, args, NULL, action_count,
                          actions, &proc, errmsg);
  if (status != ZX_OK) {
    printf("failed to start helper: %s\n", errmsg);
    return status;
  }

  status = zx_object_wait_one(proc, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE, NULL);
  if (status != ZX_OK) {
    printf("zx_object_wait_one: %s\n", zx_status_get_string(status));
    return status;
  }
  zx_info_process_t info;
  status = zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL);
  if (status != ZX_OK) {
    printf("zx_object_get_info: %s\n", zx_status_get_string(status));
    return status;
  }

  return (int)info.return_code;
}
