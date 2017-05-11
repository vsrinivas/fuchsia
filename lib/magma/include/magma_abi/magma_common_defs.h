// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_SYSTEM_COMMON_DEFS_H_
#define _MAGMA_SYSTEM_COMMON_DEFS_H_

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define MAGMA_CAPABILITY_RENDERING 1
#define MAGMA_CAPABILITY_DISPLAY 2

#define MAGMA_QUERY_DEVICE_ID 1
#define MAGMA_QUERY_VENDOR_PARAM_0 10000

#define MAGMA_DOMAIN_CPU 0x00000001
#define MAGMA_DOMAIN_GTT 0x00000040

// possible values for magma_status_t
#define MAGMA_STATUS_OK (0)
#define MAGMA_STATUS_INTERNAL_ERROR (-1)
#define MAGMA_STATUS_INVALID_ARGS (-2)
#define MAGMA_STATUS_ACCESS_DENIED (-3)
#define MAGMA_STATUS_MEMORY_ERROR (-4)
#define MAGMA_STATUS_CONTEXT_KILLED (-5)
#define MAGMA_STATUS_CONNECTION_LOST (-6)
#define MAGMA_STATUS_TIMED_OUT (-7)

// possible values for magma_image_tiling_t
#define MAGMA_IMAGE_TILING_OPTIMAL 0
#define MAGMA_IMAGE_TILING_LINEAR 1

typedef int32_t magma_status_t;

typedef uint32_t magma_image_tiling_t;

typedef uintptr_t magma_buffer_t;

typedef uintptr_t magma_semaphore_t;

struct magma_connection_t {
    uint32_t magic_;
};

// a relocation entry that informs the system driver how to patch GPU virtual addresses
// in an exec resource. The 32 bit word at offset in the buffer will be overwritten with
// the GPU virtual address of the 32 bit word at target_offset in target_buffer.
struct magma_system_relocation_entry {
    uint32_t offset;                 // offset in the batch buffer
    uint32_t target_resource_index;  // resource index of the buffer to be relocated
    uint32_t target_offset;          // offset in the target buffer
    uint32_t read_domains_bitfield;  // memory domains in which the target is readable
    uint32_t write_domains_bitfield; // memory domains in which the target is writable
};

// a buffer plus its associated relocations referenced by a command buffer
struct magma_system_exec_resource {
    uint64_t buffer_id;
    uint32_t num_relocations;
    uint64_t offset;
    uint64_t length;
};

// A batch buffer to be executed plus the resources required to execute it
struct magma_system_command_buffer {
    uint32_t batch_buffer_resource_index; // resource index of the batch buffer to execute
    uint32_t batch_start_offset;          // relative to the starting offset of the buffer
    uint32_t num_resources;
    uint32_t wait_semaphore_count;
    uint32_t signal_semaphore_count;
};

// callback type for magma_system_pageflip and msd_device_pageflip
// |status| indicates whether an error occurred
// |data| is a user defined parameter which is passed into the page flip function
typedef void (*magma_system_pageflip_callback_t)(magma_status_t status, void* data);

struct magma_system_connection_request {
    uint32_t client_id;
    uint32_t capabilities;
} __attribute__((packed));

struct magma_system_image_descriptor {
    magma_image_tiling_t tiling;
} __attribute__((packed));

struct magma_display_size {
    uint32_t width;
    uint32_t height;
};

#if defined(__cplusplus)
}
#endif

#endif // _MAGMA_SYSTEM_COMMON_DEFS_H_
