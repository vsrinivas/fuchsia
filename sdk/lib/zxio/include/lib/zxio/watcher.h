// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_WATCHER_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_WATCHER_H_

#include <lib/zxio/types.h>
#include <zircon/availability.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// Call the provided callback |cb| for each file in directory and each time
// filesystem event happens in the directory such as adding or removing a file.
//
// If the callback returns a status other than ZX_OK, watching stops and the
// callback's status is returned to the caller of zxio_watch_directory.
//
// If the deadline expires, ZX_ERR_TIMED_OUT is returned to the caller. A
// deadline of ZX_TIME_INFINITE will never expire.
//
// The callback may use ZX_ERR_STOP as a way to signal to the caller that it
// wants to stop because it found what it was looking for, etc -- since this
// error code is not returned by syscalls or public APIs, the callback does not
// need to worry about it turning up normally.

zx_status_t zxio_watch_directory(zxio_t* directory, zxio_watch_directory_cb cb, zx_time_t deadline,
                                 void* context) ZX_AVAILABLE_SINCE(7);

__END_CDECLS

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_WATCHER_H_
