// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_H_
#define _MAGMA_H_

#include "magma_common_defs.h"
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

// Opens a device - triggered by a client action. returns null on failure
// |capabilities| must be either MAGMA_SYSTEM_CAPABILITY_RENDERING, MAGMA_SYSTEM_CAPABILITY_DISPLAY,
// or the bitwise or of both
struct magma_connection_t* magma_open(int32_t file_descriptor, uint32_t capabilities);
void magma_close(struct magma_connection_t* connection);

// Returns the first recorded error since the last time this function was called.
// Clears the recorded error.
magma_status_t magma_get_error(struct magma_connection_t* connection);

// Performs a query.
// |id| is one of MAGMA_QUERY_DEVICE_ID, or a vendor-specific id starting from
// MAGMA_QUERY_FIRST_VENDOR_ID.
magma_status_t magma_query(int fd, uint64_t id, uint64_t* value_out);

void magma_create_context(struct magma_connection_t* connection, uint32_t* context_id_out);
void magma_destroy_context(struct magma_connection_t* connection, uint32_t context_id);

magma_status_t magma_alloc(struct magma_connection_t* connection, uint64_t size, uint64_t* size_out,
                           magma_buffer_t* buffer_out);
void magma_free(struct magma_connection_t* connection, magma_buffer_t buffer);

uint64_t magma_get_buffer_id(magma_buffer_t buffer);
uint64_t magma_get_buffer_size(magma_buffer_t buffer);

magma_status_t magma_map(struct magma_connection_t* connection, magma_buffer_t buffer,
                         void** addr_out);
magma_status_t magma_unmap(struct magma_connection_t* connection, magma_buffer_t buffer);

// Executes a command buffer.
// Note that the buffer referred to by |command_buffer| must contain a valid
// magma_system_command_buffer and all associated data structures
void magma_submit_command_buffer(struct magma_connection_t* connection,
                                 magma_buffer_t command_buffer, uint32_t context_id);

void magma_wait_rendering(struct magma_connection_t* connection, magma_buffer_t buffer);

// makes the buffer returned by |buffer| able to be imported via |buffer_handle_out|
magma_status_t magma_export(struct magma_connection_t* connection, magma_buffer_t buffer,
                            uint32_t* buffer_handle_out);

// imports the buffer referred to by |buffer_handle| and makes it accessible via |buffer_out|
magma_status_t magma_import(struct magma_connection_t* connection, uint32_t buffer_handle,
                            magma_buffer_t* buffer_out);

// Provides a buffer to be scanned out on the next vblank event.
// |wait_semaphores| will be waited upon prior to scanning out the buffer.
// |signal_semaphores| will be signaled when |buf| is no longer being displayed and is safe to be
// reused.
void magma_display_page_flip(struct magma_connection_t* connection, magma_buffer_t buffer,
                             uint32_t wait_semaphore_count,
                             const magma_semaphore_t* wait_semaphores,
                             uint32_t signal_semaphore_count,
                             const magma_semaphore_t* signal_semaphores);

// Creates a semaphore on the given connection.  If successful |semaphore_out| will be set.
magma_status_t magma_create_semaphore(magma_connection_t* connection,
                                      magma_semaphore_t* semaphore_out);

// Destroys |semaphore|.
void magma_destroy_semaphore(magma_connection_t* connection, magma_semaphore_t semaphore);

// Returns the object id for the given semaphore.
uint64_t magma_get_semaphore_id(magma_semaphore_t semaphore);

// Signals |semaphore|.
void magma_signal_semaphore(magma_semaphore_t semaphore);

// Resets |semaphore|.
void magma_reset_semaphore(magma_semaphore_t semaphore);

// Waits for |semaphore| to be signaled.  Returns MAGMA_STATUS_TIMED_OUT if the timeout
// expires first.
magma_status_t magma_wait_semaphore(magma_semaphore_t semaphore, uint64_t timeout);

#if defined(__cplusplus)
}
#endif

#endif /* _MAGMA_H_ */
