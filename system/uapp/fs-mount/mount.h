// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <magenta/device/devmgr.h>

#define HEADER_SIZE 4096
#define xprintf(fmt...) do { if (verbose) printf(fmt); } while(0)
#define arraylen(arr) (sizeof(arr) / sizeof(arr[0]))

extern bool verbose;

typedef struct mount_options {
    bool readonly;
    bool filesystem_requested;
    int filesystem_index;
    char* devicepath;
    char* mountpath;
} mount_options_t;

// Mount a handle to a remote filesystem on a directory.
// Any future requests made through the path at "where" will be transmitted to
// the handle returned from this function.
mx_status_t mount_remote_handle(const char* where, mx_handle_t* h);

// Use launchpad to launch a filesystem process.
int launch(int argc, const char** argv, mx_handle_t h);

#define REGISTER_FILESYSTEM(FS) \
    bool FS ## _detect(const uint8_t* data); \
    int FS ## _mount(mount_options_t* options);

REGISTER_FILESYSTEM(minfs);
REGISTER_FILESYSTEM(fat);
