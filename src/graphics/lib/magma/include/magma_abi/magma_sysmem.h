// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_INCLUDE_MAGMA_ABI_MAGMA_SYSMEM_H_
#define SRC_GRAPHICS_LIB_MAGMA_INCLUDE_MAGMA_ABI_MAGMA_SYSMEM_H_

#include <stdint.h>

#include "magma_common_defs.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Import and take ownership of a sysmem connection.
magma_status_t magma_sysmem_connection_import(magma_handle_t handle,
                                              magma_sysmem_connection_t* connection_out);

// Destroy a connection to the sysmem service. Allocated buffers are allowed to outlive the
// connection.
void magma_sysmem_connection_release(magma_sysmem_connection_t connection);

// Allocate a buffer.
magma_status_t magma_sysmem_allocate_buffer(magma_sysmem_connection_t connection, uint32_t flags,
                                            uint64_t size, uint32_t* buffer_handle_out);

void magma_buffer_format_description_release(magma_buffer_format_description_t description);

// |image_planes_out| must be an array with MAGMA_MAX_IMAGE_PLANES elements.
magma_status_t magma_get_buffer_format_plane_info_with_size(
    magma_buffer_format_description_t description, uint32_t width, uint32_t height,
    magma_image_plane_t* image_planes_out);

// Get the MAGMA_FORMAT_* value for a buffer description. May give MAGMA_FORMAT_INVALID if the
// buffer isn't an image.
magma_status_t magma_get_buffer_format(magma_buffer_format_description_t description,
                                       uint32_t* format_out);

magma_status_t magma_get_buffer_format_modifier(magma_buffer_format_description_t description,
                                                magma_bool_t* has_format_modifier_out,
                                                uint64_t* format_modifier_out);

// Get the first allowable color space for a buffer.
magma_status_t magma_get_buffer_color_space(magma_buffer_format_description_t description,
                                            uint32_t* color_space_out);

magma_status_t magma_get_buffer_coherency_domain(magma_buffer_format_description_t description,
                                                 uint32_t* coherency_domain_out);

// Get the number of buffers allocated in a buffer collection.
magma_status_t magma_get_buffer_count(magma_buffer_format_description_t description,
                                      uint32_t* count_out);

// Sets |is_secure_out| if the buffers in the collection are secure; false otherwise.
magma_status_t magma_get_buffer_is_secure(magma_buffer_format_description_t description,
                                          magma_bool_t* is_secure_out);

// Import a magma buffer collection from BufferCollectionToken handle. If the
// handle is ZX_HANDLE_INVALID (0), then a new buffer collection is created.
magma_status_t magma_buffer_collection_import(magma_sysmem_connection_t connection, uint32_t handle,
                                              magma_buffer_collection_t* collection_out);

void magma_buffer_collection_release(magma_sysmem_connection_t connection,
                                     magma_buffer_collection_t collection);

// Create a set of buffer constraints.
magma_status_t magma_buffer_constraints_create(
    magma_sysmem_connection_t connection,
    const magma_buffer_format_constraints_t* buffer_constraints,
    magma_sysmem_buffer_constraints_t* constraints_out);

// Add additional constraints (counts) onto an existing set of constraints.
magma_status_t magma_buffer_constraints_add_additional(
    magma_sysmem_connection_t connection, magma_sysmem_buffer_constraints_t constraints,
    const magma_buffer_format_additional_constraints_t* additional);

// Set a format slot on a buffer constraints. Any format slot may be used to create the texture.
magma_status_t magma_buffer_constraints_set_format(
    magma_sysmem_connection_t connection, magma_sysmem_buffer_constraints_t constraints,
    uint32_t index, const magma_image_format_constraints_t* format_constraints);

// Sets the list of allowable color spaces for an image format.
// |magma_buffer_constraints_set_format| must be called first.
magma_status_t magma_buffer_constraints_set_colorspaces(
    magma_sysmem_connection_t connection, magma_sysmem_buffer_constraints_t constraints,
    uint32_t index, uint32_t color_space_count, const uint32_t* color_spaces);

void magma_buffer_constraints_release(magma_sysmem_connection_t connection,
                                      magma_sysmem_buffer_constraints_t constraints);

// Set format constraints for allocating buffers in the collection.
magma_status_t magma_buffer_collection_set_constraints(
    magma_sysmem_connection_t connection, magma_buffer_collection_t collection,
    magma_sysmem_buffer_constraints_t constraints);

// Decodes an encoded fidl fuchsia.sysmem.SingleBufferSettings into a
// magma_buffer_format_description_t. On success |description_out| must later be released using
// magma_buffer_format_description_release.
magma_status_t magma_get_buffer_format_description(
    const void* image_data, uint64_t image_data_size,
    magma_buffer_format_description_t* description_out);

// Creates a buffer format description to describe a collection of allocated buffers. This will wait
// until the initial buffers in the collection are allocated. On success |description_out| must
// later be released using magma_buffer_format_description_release.
magma_status_t magma_sysmem_get_description_from_collection(
    magma_sysmem_connection_t connection, magma_buffer_collection_t collection,
    magma_buffer_format_description_t* buffer_format_description_out);

// Sets |buffer_handle_out| to a buffer handle (usable with magma_import) for the buffer at |index|
// in the allocated collection. |vmo_offset_out| will be set to the offset within the vmo that the
// image will be located at. This will wait until the initial buffers in the collection are
// allocated.
magma_status_t magma_sysmem_get_buffer_handle_from_collection(magma_sysmem_connection_t connection,
                                                              magma_buffer_collection_t collection,
                                                              uint32_t index,
                                                              uint32_t* buffer_handle_out,
                                                              uint32_t* vmo_offset_out);

#if defined(__cplusplus)
}
#endif

#endif  // SRC_GRAPHICS_LIB_MAGMA_INCLUDE_MAGMA_ABI_MAGMA_SYSMEM_H_
