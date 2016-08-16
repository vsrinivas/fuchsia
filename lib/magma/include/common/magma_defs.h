// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_DEFS_H_
#define _MAGMA_DEFS_H_

#include <stdint.h>

// TODO: Handle vendor specific tiling
// TODO: ensure these match the magma api
#define MAGMA_TILING_MODE_NONE 0
#define MAGMA_TILING_MODE_INTEL_X 1
#define MAGMA_TILING_MODE_INTEL_Y 2

#define MAGMA_DOMAIN_CPU 0x00000001
#define MAGMA_DOMAIN_GTT 0x00000040

#define BO_ALLOC_FOR_RENDER (1 << 0)

/* Opaque types */
typedef struct magma_connection drm_intel_bufmgr;
typedef struct magma_context drm_intel_context;
typedef struct magma_buffer drm_intel_bo;

struct magma_connection {
    int32_t magic_;
};

struct magma_context {
    uint32_t context_id;
    int32_t magic_;
};

/* magma_buffer is exposed to the api caller. */
struct magma_buffer {

    int32_t magic_;

    /**
     * Size in bytes of the buffer object.
     *
     * The size may be larger than the size originally requested for the
     * allocation, such as being aligned to page size.
     */
    uint64_t size;

/**
 * Virtual address for accessing the buffer data.  Only valid while
 * mapped.
 */
#ifdef __cplusplus
    void* virt;
#else
    void* virtual;
#endif

    /**
     * MM-specific handle for accessing object
     */
    uint32_t handle;

    /**
     * Last seen card virtual address (offset from the beginning of the
     * aperture) for the object.  This should be used to fill relocation
     * entries when calling drm_intel_bo_emit_reloc()
     */
    uint64_t offset64;
};

enum aub_dump_bmp_format {
    AUB_DUMP_BMP_FORMAT_8BIT = 1,
    AUB_DUMP_BMP_FORMAT_ARGB_4444 = 4,
    AUB_DUMP_BMP_FORMAT_ARGB_0888 = 6,
    AUB_DUMP_BMP_FORMAT_ARGB_8888 = 7,
};

typedef struct _drm_intel_aub_annotation {
    uint32_t type;
    uint32_t subtype;
    uint32_t ending_offset;
} drm_intel_aub_annotation;

#endif // _MAGMA_DEFS_H_
