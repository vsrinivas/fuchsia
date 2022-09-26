// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_ENV_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_ENV_H_

#include <lib/fdf/dispatcher.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// This library provides privileged operations to driver host environments for
// setting up and tearing down dispatchers
//
// Usage of this API is restricted.

typedef struct fdf_env_driver_shutdown_observer fdf_env_driver_shutdown_observer_t;

// Called when the asynchronous shutdown for all dispatchers owned by |driver| has completed.
typedef void(fdf_env_driver_shutdown_handler_t)(const void* driver,
                                                fdf_env_driver_shutdown_observer_t* observer);

// Holds context for the observer which will be called when the asynchronous shutdown
// for all dispatchers owned by a driver has completed.
//
// The client is responsible for retaining this structure in memory (and unmodified) until the
// handler runs.
struct fdf_env_driver_shutdown_observer {
  fdf_env_driver_shutdown_handler_t* handler;
};

// Same as |fdf_dispatcher_create| but allows setting the driver owner for the dispatcher.
//
// |driver| is an opaque pointer to the driver object. It will be used to uniquely identify
// the driver.
zx_status_t fdf_env_dispatcher_create_with_owner(const void* driver, uint32_t options,
                                                 const char* name, size_t name_len,
                                                 const char* scheduler_role,
                                                 size_t scheduler_role_len,
                                                 fdf_dispatcher_shutdown_observer_t* observer,
                                                 fdf_dispatcher_t** dispatcher);

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
// # Errors
//
// ZX_ERR_INVALID_ARGS: No driver matching |driver| was found.
//
// ZX_ERR_BAD_STATE: A driver shutdown observer was already registered.
zx_status_t fdf_env_shutdown_dispatchers_async(const void* driver,
                                               fdf_env_driver_shutdown_observer_t* observer);

// Destroys all dispatchers in the process.
//
// This should only be used called after all dispatchers have been shutdown.
void fdf_env_destroy_all_dispatchers(void);

// Blocks the current thread until each runtime dispatcher in the process
// is observed to have been destroyed.
//
// # Thread requirements
//
// This should not be called from a thread managed by the driver runtime,
// such as from tasks or ChannelRead callbacks.
void fdf_env_wait_until_all_dispatchers_destroyed(void);

// Notifies the runtime that we have entered a new driver context,
// such as via a Banjo call.
//
// |driver| is an opaque unique identifier for the driver.
void fdf_env_register_driver_entry(const void* driver);

// Notifies the runtime that we have exited the current driver context.
void fdf_env_register_driver_exit(void);

// Returns the driver on top of the the thread's current call stack.
// Returns NULL if no drivers are on the stack.
const void* fdf_env_get_current_driver(void);

// Returns whether the dispatcher has any queued tasks.
bool fdf_env_dispatcher_has_queued_tasks(fdf_dispatcher_t* dispatcher);

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_ENV_H_
