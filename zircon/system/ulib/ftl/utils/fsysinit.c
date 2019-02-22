// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <stdio.h>

#include "kernel.h"
#include "fsprivate.h"

#include <lib/backtrace-request/backtrace-request.h>

SEM FileSysSem; // Global File System Semaphore

// Called when a file system error has occurred.
int FsError(int err_code) {
    printf("FsError: %d. What follows is NOT a crash:\n", err_code);
    backtrace_request();
    return -1;
}

int FtlInit(void) {
    FileSysSem = semCreate("fsys sem", 1, OS_FIFO);
    if (FileSysSem == NULL)
        return -1;
    return 0;
}

