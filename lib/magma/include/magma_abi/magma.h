// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_INCLUDE_MAGMA_ABI_MAGMA_H_
#define GARNET_LIB_MAGMA_INCLUDE_MAGMA_ABI_MAGMA_H_

#include "magma_common_defs.h"
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

// Opens a device - triggered by a client action. returns null on failure
// |capabilities| must be either MAGMA_SYSTEM_CAPABILITY_RENDERING, MAGMA_SYSTEM_CAPABILITY_DISPLAY,
// or the bitwise or of both
struct magma_connection_t* magma_create_connection(int32_t file_descriptor, uint32_t capabilities);
void magma_release_connection(struct magma_connection_t* connection);

// Returns the first recorded error since the last time this function was called.
// Clears the recorded error.
magma_status_t magma_get_error(struct magma_connection_t* connection);

// Performs a query.
// |id| is one of MAGMA_QUERY_DEVICE_ID, or a vendor-specific id starting from
// MAGMA_QUERY_FIRST_VENDOR_ID.
magma_status_t magma_query(int32_t fd, uint64_t id, uint64_t* value_out);

void magma_create_context(struct magma_connection_t* connection, uint32_t* context_id_out);
void magma_release_context(struct magma_connection_t* connection, uint32_t context_id);

magma_status_t magma_create_buffer(struct magma_connection_t* connection, uint64_t size,
                                   uint64_t* size_out, magma_buffer_t* buffer_out);
void magma_release_buffer(struct magma_connection_t* connection, magma_buffer_t buffer);

uint64_t magma_get_buffer_id(magma_buffer_t buffer);
uint64_t magma_get_buffer_size(magma_buffer_t buffer);

// Creates a new fd representing the notification channel for the connection.
// The fd can be polled.
int32_t magma_get_notification_channel_fd(struct magma_connection_t* connection);

// Read a notification from the channel into |buffer|. Sets |*buffer_size_out| to 0 if there are no
// messages pending.
magma_status_t magma_read_notification_channel(struct magma_connection_t* connection, void* buffer,
                                               uint64_t buffer_size, uint64_t* buffer_size_out);

// Clean the cache for a region of memory and write the contents to ram. If
// invalidate is true, then invalidates the cache lines, so reads will be from
// memory.
magma_status_t magma_clean_cache(magma_buffer_t buffer, uint64_t offset, uint64_t size,
                                 magma_cache_operation_t operation);

// This must be set before the buffer is mapped anywhere.
magma_status_t magma_set_cache_policy(magma_buffer_t buffer, magma_cache_policy_t policy);

magma_status_t magma_map(struct magma_connection_t* connection, magma_buffer_t buffer,
                         void** addr_out);
// alignment must be a power of 2 and at least PAGE_SIZE.
magma_status_t magma_map_aligned(struct magma_connection_t* connection, magma_buffer_t buffer,
                                 uint64_t alignment, void** addr_out);

// Attempt to map the buffer at a specific address. Fails if the buffer was
// previously mapped or if something already is at that address.
magma_status_t magma_map_specific(struct magma_connection_t* connection, magma_buffer_t buffer,
                                  uint64_t addr);
magma_status_t magma_unmap(struct magma_connection_t* connection, magma_buffer_t buffer);

// Maps |page_count| pages of |buffer| from |page_offset| onto the GPU in the connection's address
// space at |gpu_va|. |map_flags| is a set
// of flags from MAGMA_GPU_MAP_FLAGS that specify how the GPU can access the buffer.
// TODO(MA-373): change return type to magma_status_t and perform some parameter checking.
void magma_map_buffer_gpu(struct magma_connection_t* connection, magma_buffer_t buffer,
                          uint64_t page_offset, uint64_t page_count, uint64_t gpu_va,
                          uint64_t map_flags);

// Unmaps the mapping at |gpu_va| from the GPU. Buffers will also be implicitly
// unmapped if they're released.
void magma_unmap_buffer_gpu(struct magma_connection_t* connection, magma_buffer_t buffer,
                            uint64_t gpu_va);

// Ensures the the |page_count| pages starting at |page_offset| from the
// beginning of the buffer are backed by physical memory.
// TODO(MA-373): change return type to magma_status_t and perform some parameter checking.
void magma_commit_buffer(struct magma_connection_t* connection, magma_buffer_t buffer,
                         uint64_t page_offset, uint64_t page_count);

magma_status_t magma_create_command_buffer(struct magma_connection_t* connection, uint64_t size,
                                           magma_buffer_t* buffer_out);
void magma_release_command_buffer(struct magma_connection_t* connection,
                                  magma_buffer_t command_buffer);

// Executes a command buffer.
// Note that the buffer referred to by |command_buffer| must contain a valid
// magma_system_command_buffer and all associated data structures
// Transfers ownership of |command_buffer|.
void magma_submit_command_buffer(struct magma_connection_t* connection,
                                 magma_buffer_t command_buffer, uint32_t context_id);

void magma_execute_immediate_commands(struct magma_connection_t* connection, uint32_t context_id,
                                      uint64_t command_count,
                                      struct magma_system_inline_command_buffer* command_buffers);

// makes the buffer returned by |buffer| able to be imported via |buffer_handle_out|
magma_status_t magma_export(struct magma_connection_t* connection, magma_buffer_t buffer,
                            uint32_t* buffer_handle_out);

// imports the buffer referred to by |buffer_handle| and makes it accessible via |buffer_out|
magma_status_t magma_import(struct magma_connection_t* connection, uint32_t buffer_handle,
                            magma_buffer_t* buffer_out);

// Creates a semaphore on the given connection.  If successful |semaphore_out| will be set.
magma_status_t magma_create_semaphore(struct magma_connection_t* connection,
                                      magma_semaphore_t* semaphore_out);

// Destroys |semaphore|.
void magma_release_semaphore(struct magma_connection_t* connection, magma_semaphore_t semaphore);

// Returns the object id for the given semaphore.
uint64_t magma_get_semaphore_id(magma_semaphore_t semaphore);

// Signals |semaphore|.
void magma_signal_semaphore(magma_semaphore_t semaphore);

// Resets |semaphore|.
void magma_reset_semaphore(magma_semaphore_t semaphore);

// Waits for all or one of |semaphores| to be signaled.  Returns MAGMA_STATUS_TIMED_OUT if
// |timeout_ms| expires first. Does not reset any semaphores.
magma_status_t magma_wait_semaphores(const magma_semaphore_t* semaphores, uint32_t count,
                                     uint64_t timeout_ms, bool wait_all);

// Exports |semaphore| to it can be imported into another connection via |semaphore_handle_out|
magma_status_t magma_export_semaphore(struct magma_connection_t* connection,
                                      magma_semaphore_t semaphore, uint32_t* semaphore_handle_out);

// Imports the semaphore referred to by |semaphore_handle| into the given connection and makes it
// accessible via |semaphore_out|
magma_status_t magma_import_semaphore(struct magma_connection_t* connection,
                                      uint32_t semaphore_handle, magma_semaphore_t* semaphore_out);

#if defined(__cplusplus)
}
#endif

#endif /* _MAGMA_H_ */
