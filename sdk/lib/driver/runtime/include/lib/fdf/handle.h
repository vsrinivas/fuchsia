// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_HANDLE_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_HANDLE_H_

#include <lib/fdf/types.h>

__BEGIN_CDECLS

// Closes a handle, causing the underlying object to be reclaimed by the runtime
// if no other handles to it exist.
//
// If there is a pending callback registered (such as via |fdf_channel_wait_async|),
// it must be cancelled before this is called. For unsynchronized dispatchers,
// cancellation is not considered complete until the callback is invoked.
//
// It is not an error to close the special "never a valid handle" |FDF_HANDLE_INVALID|,
// similar to free(NULL) being a valid call.
//
// Closing the last handle to a peered object using |fdf_handle_close| can affect the
// state of the object's peer (if any).
//
// This operation is thread-safe.
void fdf_handle_close(fdf_handle_t handle);

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_HANDLE_H_
