// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/backtrace-request/backtrace-request.h>
#include <stdio.h>
#include <stdlib.h>

#include "ftl_private.h"
#include "kernel.h"

SEM FileSysSem;         // Global File System Semaphore
static int g_fs_error;  // File system error code (FsError enum).

// Called when a file system error has occurred.
int FsError(int err_code) {
  printf("FsError: %d. What follows is NOT a crash:\n", err_code);
  backtrace_request();
  return -1;
}

// Called when a file system error has occurred.
int FsError2(int fs_err_code, int errno_code) {
  SetFsErrCode(fs_err_code);
  return -1;
}

int GetFsErrCode() { return g_fs_error; }

void SetFsErrCode(int error) { g_fs_error = error; }

int FtlInit(void) {
  FileSysSem = semCreate("fsys sem", 1, OS_FIFO);
  if (FileSysSem == NULL) {
    return -1;
  }

  return 0;
}
