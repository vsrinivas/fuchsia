// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <mxio/io.h>

__BEGIN_CDECLS

typedef mx_status_t (*watchdir_func_t)(int dirfd, int event, const char* fn, void* cookie);

// This event occurs when a file is added or removed, including
// (for mxio_watch_directory()) files that already exist.
#define WATCH_EVENT_ADD_FILE 1
#define WATCH_EVENT_REMOVE_FILE 2

// This event occurs, once, when mxio_watch_directory() runs
// out of existing files and has to start waiting for new
// files to be added.
#define WATCH_EVENT_IDLE 3

// Call the provided callback (cb) for each file in directory
// and each time a new file is added to the directory.
//
// If the callback returns a status other than MX_OK, watching
// stops and the callback's status is returned to the caller
// of mxio_watch_directory.
//
// If the deadline expires, MX_ERR_TIMED_OUT is returned to the
// caller.  A deadline of MX_TIME_INFINITE will never expire.
//
// The callback may use MX_ERR_STOP as a way to signal to the
// caller that it wants to stop because it found what it was
// looking for, etc -- since this error code is not returned
// by syscalls or public APIs, the callback does not need to
// worry about it turning up normally.

mx_status_t mxio_watch_directory(int dirfd, watchdir_func_t cb, mx_time_t deadline, void* cookie);


__END_CDECLS
