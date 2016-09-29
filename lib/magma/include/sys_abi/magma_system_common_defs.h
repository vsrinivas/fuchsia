// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_SYSTEM_COMMON_DEFS_H_
#define _MAGMA_SYSTEM_COMMON_DEFS_H_

#include "magma_common_defs.h"
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

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
    uint32_t buffer_handle;
    uint32_t num_relocations;
    struct magma_system_relocation_entry* relocations;
};

// A batch buffer to be executed plus the resources required to execute it
struct magma_system_command_buffer {
    uint32_t batch_buffer_resource_index; // resource index of the batch buffer to execute
    uint32_t num_resources;
    struct magma_system_exec_resource* resources;
};

// callback type for magma_system_pageflip and msd_device_pageflip
// |error| is a value from errno.h indicating the result of the attempted pageflip,
// where 0 indicates success
// |data| is a user defined parameter which is passed into the page flip function
typedef void (*magma_system_pageflip_callback_t)(int32_t error, void* data);

#if defined(__cplusplus)
}
#endif

#endif // _MAGMA_SYSTEM_COMMON_DEFS_H_