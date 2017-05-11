// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once
#include <iovec.h>
#include <sys/types.h>
#include <kernel/vm.h>

__BEGIN_CDECLS

// This library exists to calculate memory ranges to be used as arenas
// for the pmm based on a predefined memory limit. The limit is passed in
// MB via the kernel.memory-limit-mb cmdline argument, and state is held
// in a given mem_limit_ctx_t structure. The user is expected to use the
// returned io vectors to construct pmm_arena_info_t structures appropriate
// for their platform and priority levels.
typedef struct mem_limit_ctx {
    // kernel load address, set by caller
    uintptr_t kernel_base;
    // kernel size in bytes, set by claler
    size_t kernel_size;
    // ramdisk load address, set by caller
    uintptr_t ramdisk_base;
    // ramdisk size in bytes, set by caller
    size_t ramdisk_size;
    // remaining bytes in memory limit, set by init and modified by get_iovs
    size_t memory_limit;
    // true if the kernel was found in ranges, set by get_iovs
    bool found_kernel;
    // true if the ramdisk was found in ranges, set by get_iovs
    bool found_ramdisk;
} mem_limit_ctx_t;

// Returns io vectors corresponding to memory ranges to include in the pmm
// arena list based on the location of the kernel and memory limit imposed.
// This will take a contiguous range of memory and return io vectors
// corresponding to the arenas that needed to be carved out due to placement of
// the kernel, the ramdisk, and any memory limits being imposed
// upon the system. The size of the vectors returned is subtracted from
// ctx->memory_limit to maintain state for future calls. The same is done
// for found_kernel and found_ramdisk within ctx.
//
// @ctx A pointer to a ctx structure with kernel/ramdisk/limit values filled in.
// @range_base: the start address of the range.
// @range_size: size of the range in bytes.
// @iovs[]: array of iovecs to stored returned vectors. Must have two entries.
// @used_cnt: the number of entries filled in and returned in iovs[].
//
// Returns NO_ERROR on completion, and ERR_INVALID_ARGS if parameters are
// invalid.
status_t mem_limit_get_iovs(mem_limit_ctx_t* ctx, uintptr_t range_base, size_t range_size,
                            iovec_t iovs[], size_t* used_cnt);

// This is a higher level helper function for users of the library if they have
// no special constraints around memory arenas needing special flags,
// priorities, or names.
//
// @ctx A pointer to a ctx structure with kernel/ramdisk/limit values filled in.
// @range_base: the start address of the range.
// @range_size: size of the range in bytes
// @arena_template: a structure containing the default values for flags,
// priority, and name used for arenas created by this function.
//
// Returns NO_ERROR on completion, and ERR_INVALID_ARGS if parameters are
// invalid
status_t mem_limit_add_arenas_from_range(mem_limit_ctx_t* ctx, uintptr_t range_base,
                                 size_t range_size, pmm_arena_info_t arena_template);

// Checks if a memory limit exists and initializes the memory_limit member of ctx
// if one is found.
//
// Returns NO_ERROR on success, or ERR_UNSUPPORTED if no memory limit was passed
// via kernel.memory-limit-mb
status_t mem_limit_init(mem_limit_ctx_t* ctx);

__END_CDECLS
