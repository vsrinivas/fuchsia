// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_INTERNAL_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_INTERNAL_H_

#include <zircon/types.h>

__BEGIN_CDECLS

// Usage Notes:
//
// Usage of this API is restricted to the Driver Framework's V1 driver host.
// This is used to update the driver runtime of the current driver context
// that the driver host thread is in.
//
// Example:
//
// void some_op(zx_driver_t* driver) {
//   fdf_runtime_push_driver(driver);

//   // Dispatch driver operation.
//   ...
//
//   fdf_runtime_pop_driver();
// }
//

// Adds |driver| to the thread's current call stack.
void fdf_internal_push_driver(const void* driver);

// Removes the driver at the top of the thread's current call stack.
void fdf_internal_pop_driver();

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_INTERNAL_H_
