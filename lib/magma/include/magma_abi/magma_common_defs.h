// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_INCLUDE_MAGMA_ABI_MAGMA_COMMON_DEFS_H_
#define GARNET_LIB_MAGMA_INCLUDE_MAGMA_ABI_MAGMA_COMMON_DEFS_H_

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define MAGMA_CAPABILITY_RENDERING 1

#define MAGMA_QUERY_DEVICE_ID 1
#define MAGMA_QUERY_VENDOR_PARAM_0 10000

// possible values for magma_status_t
#define MAGMA_STATUS_OK (0)
#define MAGMA_STATUS_INTERNAL_ERROR (-1)
#define MAGMA_STATUS_INVALID_ARGS (-2)
#define MAGMA_STATUS_ACCESS_DENIED (-3)
#define MAGMA_STATUS_MEMORY_ERROR (-4)
#define MAGMA_STATUS_CONTEXT_KILLED (-5)
#define MAGMA_STATUS_CONNECTION_LOST (-6)
#define MAGMA_STATUS_TIMED_OUT (-7)
#define MAGMA_STATUS_UNIMPLEMENTED (-8)

// possible values for magma_cache_operation_t
#define MAGMA_CACHE_OPERATION_CLEAN 0
#define MAGMA_CACHE_OPERATION_CLEAN_INVALIDATE 1

// possible values for magma_cache_policy_t
#define MAGMA_CACHE_POLICY_CACHED 0
#define MAGMA_CACHE_POLICY_WRITE_COMBINING 1
#define MAGMA_CACHE_POLICY_UNCACHED 2

#define MAGMA_DUMP_TYPE_NORMAL (1 << 0)
// Dump current perf counters and disable them
#define MAGMA_DUMP_TYPE_PERF_COUNTERS (1 << 1)
// Start perf counter recording
#define MAGMA_DUMP_TYPE_PERF_COUNTER_ENABLE (1 << 2)

typedef int32_t magma_status_t;

// Normal bool doesn't have to be a particular size.
typedef uint8_t magma_bool_t;

typedef uint32_t magma_cache_operation_t;

typedef uint32_t magma_cache_policy_t;

typedef uintptr_t magma_buffer_t;

typedef uintptr_t magma_semaphore_t;

struct magma_connection_t {
    uint32_t magic_;
};

// a relocation entry that informs the system driver how to patch GPU virtual addresses
// in an exec resource. The 32 bit word at offset in the buffer will be overwritten with
// the GPU virtual address of the 32 bit word at target_offset in target_buffer.
struct magma_system_relocation_entry {
    uint32_t offset;                // offset in the batch buffer
    uint32_t target_resource_index; // resource index of the buffer to be relocated
    uint32_t target_offset;         // offset in the target buffer
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

struct magma_system_connection_request {
    uint64_t client_id;
    uint32_t capabilities;
} __attribute__((packed));

struct magma_system_inline_command_buffer {
    void* data;
    uint64_t size;
    magma_semaphore_t* semaphores;
    uint32_t semaphore_count;
};

// The top 16 bits are reserved for vendor-specific flags.
#define MAGMA_GPU_MAP_FLAG_VENDOR_SHIFT 16

enum MAGMA_GPU_MAP_FLAGS {
    MAGMA_GPU_MAP_FLAG_NONE = 0,
    MAGMA_GPU_MAP_FLAG_READ = (1 << 0),
    MAGMA_GPU_MAP_FLAG_WRITE = (1 << 1),
    MAGMA_GPU_MAP_FLAG_EXECUTE = (1 << 2),
    MAGMA_GPU_MAP_FLAG_GROWABLE = (1 << 3),

    MAGMA_GPU_MAP_FLAG_VENDOR_MASK = (0xffff << MAGMA_GPU_MAP_FLAG_VENDOR_SHIFT),
};

#if defined(__cplusplus)
}
#endif

#endif // GARNET_LIB_MAGMA_INCLUDE_MAGMA_ABI_MAGMA_COMMON_DEFS_H_
