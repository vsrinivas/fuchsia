// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fnmatch.h>
#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

const char* INSTALL_PATH = "/install";

const char* FVM_PATTERNS = "fvm.*.blk";
const char* ESP_PATTERNS = "local*.esp.blk";
const char* VBOOT_PATTERNS = "*.vboot";

const char* PAVER = "/boot/bin/install-disk-image";
const char* FVM_PAVER = "install-fvm";
const char* EFI_PAVER = "install-efi";
const char* VBOOT_PAVER = "install-kernc";
const char* FILE_FLAG = "--file";

bool pave(const char* paver, const char* file) {
  launchpad_t* lp = NULL;
  launchpad_create(zx_job_default(), PAVER, &lp);

  launchpad_load_from_file(lp, PAVER);

  char file_abs[PATH_MAX];
  sprintf(&file_abs[0], "%s/%s", INSTALL_PATH, file);
  const char* argv[4] = {PAVER, paver, FILE_FLAG, &file_abs[0]};
  launchpad_set_args(lp, 4, argv);

  const char* errmsg = NULL;
  zx_handle_t process = ZX_HANDLE_INVALID;

  launchpad_clone(lp, LP_CLONE_FDIO_ALL);

  zx_status_t status = launchpad_go(lp, &process, &errmsg);
  if (status != ZX_OK) {
    printf("Failed to launch paver: status: %d\n", status);
    return false;
  }

  zx_signals_t sigs;
  status = zx_object_wait_one(process, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE,
                              &sigs);
  if (status != ZX_OK) {
    printf("Failed to wait for paver: status: %d\n", status);
    return false;
  }

  return true;
}

int main() {
  DIR* install_dir = opendir(INSTALL_PATH);
  if (install_dir == NULL) {
    perror("Failed to open install sources directory");
    return 1;
  }

  struct dirent* de;
  while ((de = readdir(install_dir)) != NULL) {
    if (de->d_name[0] == '.' && de->d_name[1] == 0) {
      continue;
    }
    if (fnmatch(FVM_PATTERNS, de->d_name, FNM_NOESCAPE) == 0) {
      if (!pave(FVM_PAVER, de->d_name)) {
        return 1;
      }
      continue;
    }
    if (fnmatch(ESP_PATTERNS, de->d_name, FNM_NOESCAPE) == 0) {
      if (!pave(EFI_PAVER, de->d_name)) {
        return 1;
      }
      continue;
    }
    if (fnmatch(VBOOT_PATTERNS, de->d_name, FNM_NOESCAPE) == 0) {
      if (!pave(VBOOT_PAVER, de->d_name)) {
        return 1;
      }
      continue;
    }
    printf("Unknown installer source: %s\n", de->d_name);
  }

  closedir(install_dir);
}