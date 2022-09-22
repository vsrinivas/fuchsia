// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_INTERNAL_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_INTERNAL_H_

#include <lib/fdf/dispatcher.h>
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

typedef struct fdf_internal_driver_shutdown_observer fdf_internal_driver_shutdown_observer_t;

// Called when the asynchronous shutdown for all dispatchers owned by |driver| has completed.
typedef void(fdf_internal_driver_shutdown_handler_t)(
    const void* driver, fdf_internal_driver_shutdown_observer_t* observer);

// Holds context for the observer which will be called when the asynchronous shutdown
// for all dispatchers owned by a driver has completed.
//
// The client is responsible for retaining this structure in memory (and unmodified) until the
// handler runs.
struct fdf_internal_driver_shutdown_observer {
  fdf_internal_driver_shutdown_handler_t* handler;
};

// Adds |driver| to the thread's current call stack.
void fdf_internal_push_driver(const void* driver);

// Removes the driver at the top of the thread's current call stack.
void fdf_internal_pop_driver();

// Creates a |fdf_dispatcher_t| which is owned by |driver|.
//
// |driver| is an opaque pointer to the driver object. It will be used to uniquely identify
// the driver.
zx_status_t fdf_internal_dispatcher_create_with_owner(const void* driver, uint32_t options,
                                                      const char* name, size_t name_len,
                                                      const char* scheduler_role,
                                                      size_t scheduler_role_len,
                                                      fdf_dispatcher_shutdown_observer_t* observer,
                                                      fdf_dispatcher_t** dispatcher);

// Returns the driver on top of the the thread's current call stack.
// Returns NULL if no drivers are on the stack.
const void* fdf_internal_get_current_driver();

// Destroys all dispatchers in the process.
// This should only be used by the driver host after it has successfully shutdown
// all dispatchers and stopped all drivers.
void fdf_internal_destroy_all_dispatchers();

// Returns whether the dispatcher has any queued tasks.
bool fdf_internal_dispatcher_has_queued_tasks(fdf_dispatcher_t* dispatcher);

// Blocks the current thread until each runtime dispatcher in the process
// is observed to have been destroyed.
// This should not be called from a thread managed by the driver runtime,
// such as from tasks or ChannelRead callbacks.
void fdf_internal_wait_until_all_dispatchers_destroyed();

// Asynchronously shuts down all dispatchers owned by |driver|.
// |observer| will be notified once shutdown completes. This is guaranteed to be
// after all the dispatcher's shutdown observers have been called, and will be running
// on the thread of the final dispatcher which has been shutdown.
//
// after all dispatcher's shutdown observers have had their handlers called.
// While a driver is shutting down, no new dispatchers can be created by the driver.
//
// If this succeeds, you must keep the |observer| object alive until the
// |observer| is notified.
//
// Returns ZX_OK if successful and |observer| will be notified.
// Returns ZX_ERR_INVALID_ARGS if no driver matching |driver| was found.
// Returns ZX_ERR_BAD_STATE if a driver shutdown observer was already registered.
zx_status_t fdf_internal_shutdown_dispatchers_async(
    const void* driver, fdf_internal_driver_shutdown_observer_t* observer);

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_INTERNAL_H_
