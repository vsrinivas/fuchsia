// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_INCLUDE_LIB_FDIO_WATCHER_H_
#define LIB_FDIO_INCLUDE_LIB_FDIO_WATCHER_H_

#include <lib/fdio/io.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

typedef zx_status_t (*watchdir_func_t)(int dirfd, int event, const char* fn, void* cookie);

// This event occurs when a file is added or removed, including
// (for fdio_watch_directory()) files that already exist.
#define WATCH_EVENT_ADD_FILE 1
#define WATCH_EVENT_REMOVE_FILE 2

// This event occurs, once, when fdio_watch_directory() runs
// out of existing files and has to start waiting for new
// files to be added.
#define WATCH_EVENT_WAITING 3

// Call the provided callback (cb) for each file in directory
// and each time a new file is added to the directory.
//
// If the callback returns a status other than ZX_OK, watching
// stops and the callback's status is returned to the caller
// of fdio_watch_directory.
//
// If the deadline expires, ZX_ERR_TIMED_OUT is returned to the
// caller.  A deadline of ZX_TIME_INFINITE will never expire.
//
// The callback may use ZX_ERR_STOP as a way to signal to the
// caller that it wants to stop because it found what it was
// looking for, etc -- since this error code is not returned
// by syscalls or public APIs, the callback does not need to
// worry about it turning up normally.

zx_status_t fdio_watch_directory(int dirfd, watchdir_func_t cb, zx_time_t deadline, void* cookie);

__END_CDECLS

#endif  // LIB_FDIO_INCLUDE_LIB_FDIO_WATCHER_H_
