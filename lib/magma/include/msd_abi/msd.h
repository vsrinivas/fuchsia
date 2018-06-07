// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_INCLUDE_MSD_ABI_MSD_H_
#define GARNET_LIB_MAGMA_INCLUDE_MSD_ABI_MSD_H_

#include "msd_defs.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Instantiates a driver instance.
struct msd_driver_t* msd_driver_create(void);

// Configures the driver according to |flags|.
void msd_driver_configure(struct msd_driver_t* drv, uint32_t flags);

// Destroys a driver instance.
void msd_driver_destroy(struct msd_driver_t* drv);

// Creates a device at system startup.
struct msd_device_t* msd_driver_create_device(struct msd_driver_t* drv, void* device);

// Destroys a device at system shutdown.
void msd_device_destroy(struct msd_device_t* dev);

// Returns a value associated with the given id.
magma_status_t msd_device_query(struct msd_device_t* device, uint64_t id, uint64_t* value_out);

void msd_device_dump_status(struct msd_device_t* dev);

// Opens a device for the given client. Returns null on failure
struct msd_connection_t* msd_device_open(struct msd_device_t* dev, msd_client_id_t client_id);

// Closes the given connection to the device.
void msd_connection_close(struct msd_connection_t* connection);

magma_status_t msd_connection_map_buffer_gpu(struct msd_connection_t* connection,
                                             struct msd_buffer_t* buffer, uint64_t gpu_va,
                                             uint64_t page_offset, uint64_t page_count,
                                             uint64_t flags);
magma_status_t msd_connection_unmap_buffer_gpu(struct msd_connection_t* connection,
                                               struct msd_buffer_t* buffer, uint64_t gpu_va);
magma_status_t msd_connection_commit_buffer(struct msd_connection_t* connection,
                                            struct msd_buffer_t* buffer, uint64_t page_offset,
                                            uint64_t page_count);

// Sets the callback to be used by a connection for various notifications.
// This is called when a connection is created, and also called to unset
// the callback before a connection is destroyed.  A multithreaded
// implementation must be careful to guard use of this callback to avoid
// collision with possible concurrent destruction.
void msd_connection_set_notification_callback(struct msd_connection_t* connection,
                                              msd_connection_notification_callback_t callback,
                                              void* token);

// Creates a context for the given connection. returns null on failure.
struct msd_context_t* msd_connection_create_context(struct msd_connection_t* connection);

// Destroys the given context.
void msd_context_destroy(struct msd_context_t* ctx);

// returns 0 on success
// |ctx| is the context in which to execute the command buffer
// |cmd_buf| is the command buffer to be executed
// |exec_resources| are the buffers referenced by the handles in command_buf->exec_resources,
// in the same order
// |wait_semaphores| are the semaphores that must be signaled before starting command buffer
// execution
// |signal_semaphores| are the semaphores to be signaled upon completion of the command buffer
magma_status_t msd_context_execute_command_buffer(struct msd_context_t* ctx,
                                                  struct msd_buffer_t* cmd_buf,
                                                  struct msd_buffer_t** exec_resources,
                                                  struct msd_semaphore_t** wait_semaphores,
                                                  struct msd_semaphore_t** signal_semaphores);

// Executes a buffer of commands of |commands_size| bytes.
magma_status_t msd_context_execute_immediate_commands(struct msd_context_t* ctx,
                                                      uint64_t commands_size, void* commands,
                                                      uint64_t semaphore_count,
                                                      struct msd_semaphore_t** semaphores);

// Signals that the given |buffer| is no longer in use on the given |context|.
// May be used to free up resources such as a cached address space mapping for the given buffer.
void msd_context_release_buffer(struct msd_context_t* context, struct msd_buffer_t* buffer);

// Signals that the given |buffer| is no longer in use on the given |connection|. This must be
// called for every connection associated with a buffer before the buffer is destroyed, or for every
// buffer associated with a connection before the connection is destroyed.
void msd_connection_release_buffer(struct msd_connection_t* connection,
                                   struct msd_buffer_t* buffer);

// Creates a buffer that owns the provided handle
// The resulting msd_buffer_t is owned by the caller and must be destroyed
// Returns NULL on failure.
struct msd_buffer_t* msd_buffer_import(uint32_t handle);

// Destroys |buf|
// This releases buf's reference to the underlying platform buffer
void msd_buffer_destroy(struct msd_buffer_t* buf);

// Imports the given handle as a semaphore.
magma_status_t msd_semaphore_import(uint32_t handle, struct msd_semaphore_t** semaphore_out);

// Releases the given semaphore.
void msd_semaphore_release(struct msd_semaphore_t* semaphore);

#if defined(__cplusplus)
}
#endif

#endif  // GARNET_LIB_MAGMA_INCLUDE_MSD_ABI_MSD_H_
