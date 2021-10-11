// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_DISPATCHER_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_DISPATCHER_H_

#include <lib/async/dispatcher.h>
#include <lib/fdf/types.h>

__BEGIN_CDECLS

// Usage Notes:
//
// fdf_dispatcher_t can be used in conjunction with a fdf_channel_t
// to initiate asynchronous read operations. The dispatcher is in
// charge of dispatching the read callbacks.
//
// Example:
//   TODO(fxb/85946): update this once scheduler_role is supported.
//   const char* scheduler_role = "";
//
//   fdf_dispatcher_t* dispatcher;
//   fdf_status_t status =
//       fdf_dispatcher_create(0, scheduler_role, strlen(scheduler_role), &dispatcher);
//
//   fdf_channel_read_t channel_read;
//   ...
//   status = fdf_channel_wait_async(dispatcher, channel_read, 0);
//
//   // The dispatcher will call the channel_read handler when ready.
//
//   fdf_dispatcher_destroy(dispatcher);
//
typedef struct fdf_dispatcher fdf_dispatcher_t;

// Defined in <lib/ddk/driver.h>
struct zx_driver;

// This flag allows parallel calls into callbacks set in the dispatcher.
// Cannot be set in conjunction with FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS.
#define FDF_DISPATCHER_OPTION_UNSYNCHRONIZED (1 << 0)
// This flag indicates that the dispatcher may not share zircon threads with other drivers.
// Cannot be set in conjunction with FDF_DISPATCHER_OPTION_UNSYNCHRONIZED.
#define FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS (1 << 1)

// |scheduler_role| is a hint. It may or not impact the priority the work scheduler against the
// dispatcher is handled at. It may or may not impact the ability for other drivers to share zircon
// threads with the dispatcher.
// |scheduler_role_len | is the length of the string, without including the terminating
// NULL character.
// TODO(fxb/85946): currently |scheduler_role| is not implemented.
fdf_status_t fdf_dispatcher_create(uint32_t options, const char* scheduler_role,
                                   size_t scheduler_role_len, fdf_dispatcher_t** dispatcher);

// Returns the asynchronous dispatch interface.
async_dispatcher_t* fdf_dispatcher_get_async_dispatcher(fdf_dispatcher_t* dispatcher);

// Destroys the dispatcher. Joins with all threads spawned by the dispatcher.
void fdf_dispatcher_destroy(fdf_dispatcher_t* dispatcher);

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_DISPATCHER_H_
