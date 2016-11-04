// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_SYSTEM_H_
#define _MAGMA_SYSTEM_H_

#include "magma_system_common_defs.h"
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct magma_system_connection {
    uint32_t magic_;
};

// Functions on this interface that return integers return 0 on success unless otherwise noted
// Error codes can be found in errno.h

// Opens a device - triggered by a client action. returns null on failure
// |capabilities| must be either MAGMA_SYSTEM_CAPABILITY_RENDERING, MAGMA_SYSTEM_CAPABILITY_DISPLAY,
// or the bitwise or of both
struct magma_system_connection* magma_system_open(int32_t file_descriptor, uint32_t capabilities);
void magma_system_close(struct magma_system_connection* connection);

// Returns the first recorded error since the last time this function was called.
// Clears the recorded error.
int32_t magma_system_get_error(struct magma_system_connection* connection);

// Returns the device id.  0 is an invalid device id.
uint32_t magma_system_get_device_id(int fd);

void magma_system_create_context(struct magma_system_connection* connection,
                                 uint32_t* context_id_out);
void magma_system_destroy_context(struct magma_system_connection* connection, uint32_t context_id);

int32_t magma_system_alloc(struct magma_system_connection* connection, uint64_t size,
                           uint64_t* size_out, uint32_t* buffer_id_out);
void magma_system_free(struct magma_system_connection* connection, uint32_t buffer_id);

void magma_system_set_tiling_mode(struct magma_system_connection* connection, uint32_t buffer_id,
                                  uint32_t tiling_mode);

int32_t magma_system_map(struct magma_system_connection* connection, uint32_t buffer_id,
                         void** addr_out);
int32_t magma_system_unmap(struct magma_system_connection* connection, uint32_t buffer_id,
                           void* addr);

void magma_system_set_domain(struct magma_system_connection* connection, uint32_t buffer_id,
                             uint32_t read_domains, uint32_t write_domain);

void magma_system_submit_command_buffer(struct magma_system_connection* connection,
                                        struct magma_system_command_buffer* command_buffer,
                                        uint32_t context_id);

void magma_system_wait_rendering(struct magma_system_connection* connection, uint32_t buffer_id);

// makes the buffer returned by |buffer_id| able to be imported via |buffer_handle_out|
int32_t magma_system_export(magma_system_connection* connection, uint32_t buffer_id,
                            uint32_t* buffer_handle_out);

// imports the buffer referred to by |buffer_handle| and makes it accessible via |buffer_id_out|
int32_t magma_system_import(struct magma_system_connection* connection, uint32_t buffer_handle,
                            uint32_t* buffer_id_out);

// Provides a buffer to be scanned out on the next vblank event.
// |callback| will be called with |data| as its second argument when the buffer
// referred to by |buffer_id| is no longer being displayed and is safe to be
// reused. The first argument to |callback| indicates an error with the page
// flip, where 0 indicates success
// This will fail if |connection| was not created with |MAGMA_SYSTEM_CAPABILITY_DISPLAY|
void magma_system_display_page_flip(struct magma_system_connection* connection, uint32_t buffer_id,
                                    magma_system_pageflip_callback_t callback, void* data);

#if defined(__cplusplus)
}
#endif

#endif /* _MAGMA_SYSTEM_H_ */
