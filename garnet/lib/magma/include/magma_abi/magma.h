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

// Performs a query and returns a result synchronously.
// |file_descriptor| must correspond to an open device of class gpu.
// |id| is one of MAGMA_QUERY_DEVICE_ID, or a vendor-specific id starting from
// MAGMA_QUERY_FIRST_VENDOR_ID.
// Data is returned in |value_out|.
magma_status_t magma_query(int32_t file_descriptor, uint64_t id, uint64_t* value_out);

// Opens a connection to a device and returns it in |connection_out|.
// |file_descriptor| must correspond to an open device of class gpu.
magma_status_t magma_create_connection(int32_t file_descriptor, magma_connection_t* connection_out);

// Releases the given |connection|.
void magma_release_connection(magma_connection_t connection);

// Returns the first recorded error since the last time this function was called.
// Clears the recorded error.
// Incurs a round-trip to the system driver.
magma_status_t magma_get_error(magma_connection_t connection);

// Creates a context on the given |connection| and returns an id in |context_id_out|.
// A context is required to submit command buffers.
void magma_create_context(magma_connection_t connection, uint32_t* context_id_out);

// Releases the context associated with |context_id|.
void magma_release_context(magma_connection_t connection, uint32_t context_id);

// Creates a memory buffer of at least the given |size| and returns a handle to it
// in |buffer|.
// The actual size is returned in |size_out|.
magma_status_t magma_create_buffer(magma_connection_t connection, uint64_t size, uint64_t* size_out,
                                   magma_buffer_t* buffer_out);

// Releases the given memory |buffer|.
void magma_release_buffer(magma_connection_t connection, magma_buffer_t buffer);

// Duplicates |buffer_handle|, giving another handle that can be imported into a connection.
magma_status_t magma_duplicate_handle(uint32_t buffer_handle, uint32_t* buffer_handle_out);

// Releases |buffer_handle|.
magma_status_t magma_release_buffer_handle(uint32_t buffer_handle);

// Returns a unique id for the given |buffer|.
uint64_t magma_get_buffer_id(magma_buffer_t buffer);

// Returns the size of the given |buffer|.
uint64_t magma_get_buffer_size(magma_buffer_t buffer);

// Cleans the cache for the region of memory specified by |buffer| at the given |offset|
// and |size|, and write the contents to ram.
// If |operation| is MAGMA_CACHE_OPERATION_CLEAN_INVALIDATE, then also invalidates
// the cache lines, so reads will fetch from external memory.
magma_status_t magma_clean_cache(magma_buffer_t buffer, uint64_t offset, uint64_t size,
                                 magma_cache_operation_t operation);

// Configures the cache for the given |buffer|.
// This must be set before the buffer is mapped anywhere.
magma_status_t magma_set_cache_policy(magma_buffer_t buffer, magma_cache_policy_t policy);

// Queries the cache policy for a buffer.
magma_status_t magma_get_buffer_cache_policy(magma_buffer_t buffer,
                                             magma_cache_policy_t* cache_policy_out);

// Creates a mapping for the given |buffer| on the cpu.
// The cpu virtual address is returned in |addr_out|.
// May be called multiple times.
magma_status_t magma_map(magma_connection_t connection, magma_buffer_t buffer, void** addr_out);

// Creates a mapping for the given |buffer| on the cpu with the given |alignment|, which
// must be a power of 2 and at least PAGE_SIZE. The cpu virtual address is returned
// in |addr_out|.
magma_status_t magma_map_aligned(magma_connection_t connection, magma_buffer_t buffer,
                                 uint64_t alignment, void** addr_out);

// Attempts to map the given |buffer| at a specific cpu virtual address |addr|.
// Fails if the buffer was previously mapped, or if that address is unavailable.
magma_status_t magma_map_specific(magma_connection_t connection, magma_buffer_t buffer,
                                  uint64_t addr, uint64_t offset, uint64_t length);

// Releases a cpu mapping for the given |buffer|.
// Should be paired with each call to one of the mapping interfaces.
magma_status_t magma_unmap(magma_connection_t connection, magma_buffer_t buffer);

// Maps |page_count| pages of |buffer| from |page_offset| onto the GPU in the connection's address
// space at |gpu_va|.
// |map_flags| is a set of flags from MAGMA_GPU_MAP_FLAGS that specify how the GPU can access
// the buffer.
void magma_map_buffer_gpu(magma_connection_t connection, magma_buffer_t buffer,
                          uint64_t page_offset, uint64_t page_count, uint64_t gpu_va,
                          uint64_t map_flags);

// Releases the mapping at |gpu_va| from the GPU.
// Buffers will also be implicitly unmapped when released.
void magma_unmap_buffer_gpu(magma_connection_t connection, magma_buffer_t buffer, uint64_t gpu_va);

// Ensures that |page_count| pages starting at |page_offset| from the beginning of the buffer
// are backed by physical memory.
void magma_commit_buffer(magma_connection_t connection, magma_buffer_t buffer, uint64_t page_offset,
                         uint64_t page_count);

// Exports |buffer|, returning it in |buffer_handle_out|, so it may be imported into another
// connection.
magma_status_t magma_export(magma_connection_t connection, magma_buffer_t buffer,
                            uint32_t* buffer_handle_out);

// Imports and takes ownership of the buffer referred to by |buffer_handle| into the given
// |connection| and makes it accessible via |buffer_out|.
magma_status_t magma_import(magma_connection_t connection, uint32_t buffer_handle,
                            magma_buffer_t* buffer_out);

// Creates a buffer of the given |size| that may be submitted as command buffer.
// The buffer is returned in |buffer_out|.
magma_status_t magma_create_command_buffer(magma_connection_t connection, uint64_t size,
                                           magma_buffer_t* buffer_out);

// Releases a command buffer without submitting it.
void magma_release_command_buffer(magma_connection_t connection, magma_buffer_t command_buffer);

// Submits a command buffer for execution on the GPU.
// Note that the buffer referred to by |command_buffer| must contain a valid
// magma_system_command_buffer and all associated data structures
// Transfers ownership of |command_buffer|.
void magma_submit_command_buffer(magma_connection_t connection, magma_buffer_t command_buffer,
                                 uint32_t context_id);

// Submits a series of commands for execution on the GPU without using a command buffer.
void magma_execute_immediate_commands(magma_connection_t connection, uint32_t context_id,
                                      uint64_t command_count,
                                      struct magma_system_inline_command_buffer* command_buffers);

// Creates a semaphore which is returned in |semaphore_out|.
magma_status_t magma_create_semaphore(magma_connection_t connection,
                                      magma_semaphore_t* semaphore_out);

// Releases the given |semaphore|.
void magma_release_semaphore(magma_connection_t connection, magma_semaphore_t semaphore);

// Returns the object id for the given |semaphore|.
uint64_t magma_get_semaphore_id(magma_semaphore_t semaphore);

// Signals the given |semaphore|.
void magma_signal_semaphore(magma_semaphore_t semaphore);

// Resets the given |semaphore|.
void magma_reset_semaphore(magma_semaphore_t semaphore);

// Waits for all or one of |semaphores| to be signaled.
// Returns MAGMA_STATUS_TIMED_OUT if |timeout_ms| expires first.
// Does not reset any semaphores.
magma_status_t magma_wait_semaphores(const magma_semaphore_t* semaphores, uint32_t count,
                                     uint64_t timeout_ms, magma_bool_t wait_all);

// Exports |semaphore|, returning it in |semaphore_handle_out|, so it can be imported into another
// connection.
magma_status_t magma_export_semaphore(magma_connection_t connection, magma_semaphore_t semaphore,
                                      uint32_t* semaphore_handle_out);

// Imports and takes ownership of the semaphore referred to by |semaphore_handle| into the given
// |connection| and makes it accessible via |semaphore_out|.
magma_status_t magma_import_semaphore(magma_connection_t connection, uint32_t semaphore_handle,
                                      magma_semaphore_t* semaphore_out);

// Returns a uint32_t (zx_handle_t) that can be waited on to determine when the connection has data
// in the notification channel. This channel has the same lifetime as the connection and must not be
// closed by the client.
uint32_t magma_get_notification_channel_handle(magma_connection_t connection);

// Returns MAGMA_STATUS_OK if a message is available on the notification channel before |timeout_ns|
// expires.
magma_status_t magma_wait_notification_channel(magma_connection_t connection, int64_t timeout_ns);

// Reads a notification from the channel into |buffer| which has the given |buffer_size|.
// Sets |*buffer_size_out| to 0 if there are no messages pending.
magma_status_t magma_read_notification_channel(magma_connection_t connection, void* buffer,
                                               uint64_t buffer_size, uint64_t* buffer_size_out);

#if defined(__cplusplus)
}
#endif

#endif // GARNET_LIB_MAGMA_INCLUDE_MAGMA_ABI_MAGMA_H_
