// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_TESTING_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_TESTING_H_

#include <lib/fdf/dispatcher.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Adds |driver| to the thread's current call stack.
void fdf_testing_push_driver(const void* driver);

// Removes the driver at the top of the thread's current call stack.
void fdf_testing_pop_driver(void);

// Blocks the current thread until each runtime dispatcher in the process
// is observed to enter an idle state. This does not guarantee that all the
// dispatchers will be idle when this function returns. This will only wait
// on dispatchers that existed when this function was called. This does not
// include any new dispatchers that might have been created while the waiting
// was happening.
// This does not wait for registered waits that have not yet been signaled,
// or delayed tasks which have been scheduled for a future deadline.
// This should not be called from a thread managed by the driver runtime,
// such as from tasks or ChannelRead callbacks.
void fdf_testing_wait_until_all_dispatchers_idle(void);

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_TESTING_H_
