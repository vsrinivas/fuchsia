// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_INCLUDE_MAGMA_ABI_MAGMA_SYSMEM_H_
#define GARNET_LIB_MAGMA_INCLUDE_MAGMA_ABI_MAGMA_SYSMEM_H_

#include "magma_common_defs.h"
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

// Allocate a new connection to the sysmem service.
magma_status_t magma_sysmem_connection_create(magma_sysmem_connection_t* connection_out);

// Destroy a connection to the sysmem service. Allocated buffers are allowed to outlive the
// connection.
void magma_sysmem_connection_release(magma_sysmem_connection_t connection);

// Allocate a buffer.
magma_status_t magma_sysmem_allocate_buffer(magma_sysmem_connection_t connection, uint32_t flags,
                                            uint64_t size, uint32_t* buffer_handle_out);

// Allocate a texture. |buffer_format_description_out| must later be released
// using magma_buffer_format_description_release.
magma_status_t
magma_sysmem_allocate_texture(magma_sysmem_connection_t connection, uint32_t flags, uint32_t format,
                              uint32_t width, uint32_t height, uint32_t* buffer_handle_out,
                              magma_buffer_format_description_t* buffer_format_description_out);

void magma_buffer_format_description_release(magma_buffer_format_description_t description);

// |image_planes_out| must be an array with MAGMA_MAX_IMAGE_PLANES elements.
magma_status_t magma_get_buffer_format_plane_info(magma_buffer_format_description_t description,
                                                  magma_image_plane_t* image_planes_out);

// Import a magma buffer collection from BufferCollectionToken handle.
magma_status_t magma_buffer_collection_import(magma_sysmem_connection_t connection, uint32_t handle,
                                              magma_buffer_collection_t* collection_out);

void magma_buffer_collection_release(magma_sysmem_connection_t connection,
                                     magma_buffer_collection_t collection);

// Create a set of buffer constraints.
magma_status_t
magma_buffer_constraints_create(magma_sysmem_connection_t connection,
                                const magma_buffer_format_constraints_t* buffer_constraints,
                                magma_sysmem_buffer_constraints_t* constraints_out);

// Set a format slot on a buffer constraints. Any format slot may be used to create the texture.
magma_status_t
magma_buffer_constraints_set_format(magma_sysmem_connection_t connection,
                                    magma_sysmem_buffer_constraints_t constraints, uint32_t index,
                                    const magma_image_format_constraints_t* format_constraints);

void magma_buffer_constraints_release(magma_sysmem_connection_t connection,
                                      magma_sysmem_buffer_constraints_t constraints);

// Set format constraints for allocating buffers in the collection.
magma_status_t
magma_buffer_collection_set_constraints(magma_sysmem_connection_t connection,
                                        magma_buffer_collection_t collection,
                                        magma_sysmem_buffer_constraints_t constraints);

#if defined(__cplusplus)
}
#endif

#endif // GARNET_LIB_MAGMA_INCLUDE_MAGMA_ABI_MAGMA_SYSMEM_H_
