// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <mxio/io.h>

__BEGIN_CDECLS

typedef struct mxio_watcher mxio_watcher_t;

// Create a directory watcher for the provided dirfd.
// The watcher does not take ownership of the fd and
// the fd may be closed after this call returns and
// the watcher will continue to work.
mx_status_t mxio_watcher_create(int dirfd, mxio_watcher_t** out);

// Wait until a file is added to the directory being
// watched.  Returns NO_ERROR and the name of the new
// file on success.
mx_status_t mxio_watcher_wait(mxio_watcher_t* watcher, char name[MXIO_MAX_FILENAME + 1]);

// Destroy a directory watcher.
void mxio_watcher_destroy(mxio_watcher_t* watcher);

typedef mx_status_t (*watchdir_func_t)(int dirfd, int event, const char* fn, void* cookie);

// This event occurs when a file is added, including
// (for mxio_watch_directory()) files that already exist.
#define WATCH_EVENT_ADD_FILE 1

// This event occurs, once, when mxio_watch_directory() runs
// out of existing files and has to start waiting for new
// files to be added.
#define WATCH_EVENT_WAITING 2

// Call cb for each file in directory and each time a
// new file is added to the directory, and also, first,
// for each existing file in the directory.  If cb
// returns non-zero, stop watching and return NO_ERROR.
mx_status_t mxio_watch_directory(int dirfd, watchdir_func_t cb, void* cookie);
__END_CDECLS
