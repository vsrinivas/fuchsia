// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_INCLUDE_MAGMA_MAGMA_COMMON_DEFS_H_
#define SRC_GRAPHICS_LIB_MAGMA_INCLUDE_MAGMA_MAGMA_COMMON_DEFS_H_

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

// This is a list of vendor-neutral queries that can be passed to magma_query.
// Vendor ID should be the PCI ID of the GPU vendor if possible, or the Khronos
// vendor ID otherwise.
#define MAGMA_QUERY_VENDOR_ID 0
#define MAGMA_QUERY_DEVICE_ID 1
// TODO(fxbug.dev/13126): remove this
#define MAGMA_QUERY_IS_TEST_RESTART_SUPPORTED 2
#define MAGMA_QUERY_IS_TOTAL_TIME_SUPPORTED 3
// 4 was MAGMA_QUERY_MINIMUM_MAPPABLE_ADDRESS
/// Upper 32bits: max inflight messages, lower 32bits: max inflight memory (MB)
#define MAGMA_QUERY_MAXIMUM_INFLIGHT_PARAMS 5

// All vendor-specific queries IDs that can be passed to magma_query must be >=
// MAGMA_QUERY_VENDOR_PARAM_0.
#define MAGMA_QUERY_VENDOR_PARAM_0 10000

// This is a list of vendor-neutral queries that can be passed to
// magma_query_returns_buffer.
#define MAGMA_QUERY_TOTAL_TIME 500

// reserved ID to represent an invalid object
#define MAGMA_INVALID_OBJECT_ID 0ull

// All vendor-specific command buffer flags must be >=
// MAGMA_COMMAND_BUFFER_VENDOR_FLAGS_0.
#define MAGMA_COMMAND_BUFFER_VENDOR_FLAGS_0 (1ull << 16)

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
// This error means that an object was not in the right state for an operation on it.
#define MAGMA_STATUS_BAD_STATE (-9)
#define MAGMA_STATUS_ALIAS_FOR_LAST MAGMA_STATUS_BAD_STATE

// possible values for magma_cache_operation_t
#define MAGMA_CACHE_OPERATION_CLEAN 0
#define MAGMA_CACHE_OPERATION_CLEAN_INVALIDATE 1

// possible values for magma_cache_policy_t
#define MAGMA_CACHE_POLICY_CACHED 0
#define MAGMA_CACHE_POLICY_WRITE_COMBINING 1
#define MAGMA_CACHE_POLICY_UNCACHED 2

#define MAGMA_DUMP_TYPE_NORMAL (1 << 0)

#define MAGMA_PERF_COUNTER_RESULT_DISCONTINUITY (1 << 0)

enum {
  // Values must match fuchsia.sysmem.PixelFormatType
  MAGMA_FORMAT_INVALID = 0,
  MAGMA_FORMAT_R8G8B8A8 = 1,
  MAGMA_FORMAT_BGRA32 = 101,
  MAGMA_FORMAT_I420 = 102,
  MAGMA_FORMAT_M420 = 103,
  MAGMA_FORMAT_NV12 = 104,
  MAGMA_FORMAT_YUY2 = 105,
  MAGMA_FORMAT_MJPEG = 106,
  MAGMA_FORMAT_YV12 = 107,
  MAGMA_FORMAT_BGR24 = 108,
  MAGMA_FORMAT_RGB565 = 109,
  MAGMA_FORMAT_RGB332 = 110,
  MAGMA_FORMAT_RGB2220 = 111,
  MAGMA_FORMAT_L8 = 112,
  MAGMA_FORMAT_R8 = 113,
  MAGMA_FORMAT_R8G8 = 114,
};

// These must match the fuchsia.sysmem format modifier values.
enum {
  MAGMA_FORMAT_MODIFIER_LINEAR = 0x0000000000000000,

  MAGMA_FORMAT_MODIFIER_INTEL_X_TILED = 0x0100000000000001,
  MAGMA_FORMAT_MODIFIER_INTEL_Y_TILED = 0x0100000000000002,
  MAGMA_FORMAT_MODIFIER_INTEL_YF_TILED = 0x0100000000000003,

  MAGMA_FORMAT_MODIFIER_INTEL_Y_TILED_CCS = 0x0100000001000002,
  MAGMA_FORMAT_MODIFIER_INTEL_YF_TILED_CCS = 0x0100000001000003,

  MAGMA_FORMAT_MODIFIER_ARM_YUV_BIT = 0x10,
  MAGMA_FORMAT_MODIFIER_ARM_SPLIT_BLOCK_BIT = 0x20,
  MAGMA_FORMAT_MODIFIER_ARM_SPARSE_BIT = 0x40,
  MAGMA_FORMAT_MODIFIER_ARM_BCH_BIT = 0x800,
  MAGMA_FORMAT_MODIFIER_ARM_TE_BIT = 0x1000,
  MAGMA_FORMAT_MODIFIER_ARM_TILED_HEADER_BIT = 0x2000,

  MAGMA_FORMAT_MODIFIER_ARM = 0x0800000000000000,
  MAGMA_FORMAT_MODIFIER_ARM_AFBC_16X16 = 0x0800000000000001,
  MAGMA_FORMAT_MODIFIER_ARM_AFBC_32X8 = 0x0800000000000002,
  MAGMA_FORMAT_MODIFIER_ARM_LINEAR_TE = 0x0800000000001000,
  MAGMA_FORMAT_MODIFIER_ARM_AFBC_16X16_TE = 0x0800000000001001,
  MAGMA_FORMAT_MODIFIER_ARM_AFBC_32X8_TE = 0x0800000000001002,

  MAGMA_FORMAT_MODIFIER_ARM_AFBC_16X16_YUV_TILED_HEADER = 0x0800000000002011,

  MAGMA_FORMAT_MODIFIER_ARM_AFBC_16X16_SPLIT_BLOCK_SPARSE_YUV = 0x0800000000000071,
  MAGMA_FORMAT_MODIFIER_ARM_AFBC_16X16_SPLIT_BLOCK_SPARSE_YUV_TE = 0x0800000000001071,

  MAGMA_FORMAT_MODIFIER_ARM_AFBC_16X16_SPLIT_BLOCK_SPARSE_YUV_TILED_HEADER = 0x0800000000002071,
  MAGMA_FORMAT_MODIFIER_ARM_AFBC_16X16_SPLIT_BLOCK_SPARSE_YUV_TE_TILED_HEADER = 0x0800000000003071,
};

// Must match fuchsia.sysmem.ColorSpaceType values.
enum {
  MAGMA_COLORSPACE_INVALID = 0,
  MAGMA_COLORSPACE_SRGB = 1,
  MAGMA_COLORSPACE_REC601_NTSC = 2,
  MAGMA_COLORSPACE_REC601_NTSC_FULL_RANGE = 3,
  MAGMA_COLORSPACE_REC601_PAL = 4,
  MAGMA_COLORSPACE_REC601_PAL_FULL_RANGE = 5,
  MAGMA_COLORSPACE_REC709 = 6,
  MAGMA_COLORSPACE_REC2020 = 7,
  MAGMA_COLORSPACE_REC2100 = 8,
};

enum {
  MAGMA_COHERENCY_DOMAIN_CPU = 0,
  MAGMA_COHERENCY_DOMAIN_RAM = 1,
  MAGMA_COHERENCY_DOMAIN_INACCESSIBLE = 2,
};

enum { MAGMA_POLL_TYPE_SEMAPHORE = 1, MAGMA_POLL_TYPE_HANDLE = 2 };

enum {
  MAGMA_POLL_CONDITION_READABLE = 1,
  MAGMA_POLL_CONDITION_SIGNALED = 3,
};

enum {
  // Eagerly populate GPU page tables with the pages mapping in this range, committing pages as
  // needed. This is not needed for MAGMA_GPU_MAP_FLAG_GROWABLE allocations, since the page tables
  // will be populated on demand.
  MAGMA_BUFFER_RANGE_OP_POPULATE_TABLES = 1,
  // Commit memory on the client thread. GPU page tables may not be populated. This should be used
  // before POPULATE_TABLES to ensure the expensive work of committing pages happens with the
  // correct priority and without blocking the processing in the MSD of commands from other threads
  // from the same connection.
  MAGMA_BUFFER_RANGE_OP_COMMIT = 2,
  // Depopulate GPU page table mappings for this range. This prevents the GPU from accessing pages
  // in that range, but the pages retain their contents.
  MAGMA_BUFFER_RANGE_OP_DEPOPULATE_TABLES = 3,
  // Decommit memory wholy on the client thread. This may fail if the MSD currently has the page
  // tables populated.
  MAGMA_BUFFER_RANGE_OP_DECOMMIT = 4,
};

#define MAGMA_SYSMEM_FLAG_PROTECTED (1 << 0)
#define MAGMA_SYSMEM_FLAG_DISPLAY (1 << 1)
// This flag is only used to modify the name of the buffer to signal that the client requested it
// using vkAllocateMemory or similar.
#define MAGMA_SYSMEM_FLAG_FOR_CLIENT (1 << 2)

#define MAGMA_MAX_IMAGE_PLANES 4

#define MAGMA_MAX_DRM_FORMAT_MODIFIERS 16

typedef int32_t magma_status_t;

// Normal bool doesn't have to be a particular size.
typedef uint8_t magma_bool_t;

typedef uint32_t magma_cache_operation_t;

typedef uint32_t magma_cache_policy_t;

typedef uintptr_t magma_device_t;

typedef uintptr_t magma_buffer_t;

typedef uintptr_t magma_semaphore_t;

typedef uintptr_t magma_perf_count_pool_t;

typedef struct magma_connection {
  uint32_t magic_;
} * magma_connection_t;

typedef uintptr_t magma_sysmem_connection_t;

typedef uintptr_t magma_buffer_collection_t;

typedef uintptr_t magma_sysmem_buffer_constraints_t;

typedef uintptr_t magma_buffer_format_description_t;

// Corresponds to a zx_handle_t on Fuchsia.
typedef uint32_t magma_handle_t;

typedef struct magma_poll_item {
  union {
    magma_semaphore_t semaphore;
    magma_handle_t handle;
  };
  uint32_t type;
  uint32_t condition;
  uint32_t result;
} magma_poll_item_t;

// Deprecated: a buffer plus its associated relocations referenced by a command buffer
struct magma_system_exec_resource {
  uint64_t buffer_id;
  uint64_t offset;
  uint64_t length;
};

// Deprecated: a batch buffer to be executed plus the resources required to execute it
// Ensure 8 byte alignment for semaphores and resources that may follow in a stream.
struct magma_system_command_buffer {
  uint32_t resource_count;
  uint32_t batch_buffer_resource_index;  // resource index of the batch buffer to execute
  uint64_t batch_start_offset;           // relative to the starting offset of the buffer
  uint32_t wait_semaphore_count;
  uint32_t signal_semaphore_count;
} __attribute__((__aligned__(8)));

// A buffer plus its associated relocations referenced by a command buffer
struct magma_exec_resource {
  uint64_t buffer_id;
  uint64_t offset;
  uint64_t length;
};

// A batch buffer to be executed plus the resources required to execute it
// Ensure 8 byte alignment for semaphores and resources that may follow in a stream.
struct magma_command_buffer {
  uint32_t resource_count;
  uint32_t batch_buffer_resource_index;  // resource index of the batch buffer to execute
  uint64_t batch_start_offset;           // relative to the starting offset of the buffer
  uint32_t wait_semaphore_count;
  uint32_t signal_semaphore_count;
  uint64_t flags;
} __attribute__((__aligned__(8)));

struct magma_inline_command_buffer {
  void* data;
  uint64_t size;
  uint64_t* semaphore_ids;
  uint32_t semaphore_count;
};

struct magma_total_time_query_result {
  uint64_t gpu_time_ns;        // GPU time in ns since driver start.
  uint64_t monotonic_time_ns;  // monotonic clock time measured at same time CPU time was.
};

struct magma_buffer_offset {
  uint64_t buffer_id;
  uint64_t offset;
  uint64_t length;
};

// The top 16 bits are reserved for vendor-specific flags.
#define MAGMA_GPU_MAP_FLAG_VENDOR_SHIFT 16

enum MAGMA_GPU_MAP_FLAGS {
  MAGMA_GPU_MAP_FLAG_READ = (1 << 0),
  MAGMA_GPU_MAP_FLAG_WRITE = (1 << 1),
  MAGMA_GPU_MAP_FLAG_EXECUTE = (1 << 2),
  MAGMA_GPU_MAP_FLAG_GROWABLE = (1 << 3),
  MAGMA_GPU_MAP_FLAG_VENDOR_0 = (1 << MAGMA_GPU_MAP_FLAG_VENDOR_SHIFT),
};

typedef struct magma_image_plane {
  uint32_t bytes_per_row;
  uint32_t byte_offset;
} magma_image_plane_t;

typedef struct {
  uint32_t image_format;
  magma_bool_t has_format_modifier;
  uint64_t format_modifier;
  uint32_t width;
  uint32_t height;
  uint32_t layers;
  uint32_t bytes_per_row_divisor;
  uint32_t min_bytes_per_row;
} magma_image_format_constraints_t;

typedef struct {
  // min_buffer_count
  uint32_t count;
  uint32_t usage;
  magma_bool_t secure_permitted;
  magma_bool_t secure_required;
  magma_bool_t ram_domain_supported;
  magma_bool_t cpu_domain_supported;
  uint32_t min_size_bytes;
} magma_buffer_format_constraints_t;

typedef struct {
  uint32_t max_buffer_count;
  uint32_t min_buffer_count_for_camping;
  uint32_t min_buffer_count_for_dedicated_slack;
  uint32_t min_buffer_count_for_shared_slack;
} magma_buffer_format_additional_constraints_t;

typedef struct {
  uint64_t committed_byte_count;
  uint64_t size;
} magma_buffer_info_t;

enum {
  MAGMA_IMAGE_CREATE_FLAGS_PRESENTABLE = 1,
};

typedef struct {
  // A format specified by DRM (Linux Direct Rendering Manager)
  uint64_t drm_format;
  // The last modifier must be DRM_FORMAT_MOD_INVALID
  uint64_t drm_format_modifiers[MAGMA_MAX_DRM_FORMAT_MODIFIERS];
  uint32_t width;
  uint32_t height;
  uint64_t flags;
} magma_image_create_info_t;

typedef struct {
  uint64_t plane_strides[MAGMA_MAX_IMAGE_PLANES];
  uint32_t plane_offsets[MAGMA_MAX_IMAGE_PLANES];
  uint64_t drm_format_modifier;
  uint32_t coherency_domain;
} magma_image_info_t;

#if defined(__cplusplus)
}
#endif

#endif  // SRC_GRAPHICS_LIB_MAGMA_INCLUDE_MAGMA_MAGMA_COMMON_DEFS_H_
