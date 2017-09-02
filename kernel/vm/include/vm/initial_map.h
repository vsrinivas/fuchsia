// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// initial mapping table, used in ARM builds to set up the default memory map.
// TODO: replace this with MDI driven configuration.

// some assembly #defines, need to match the structure below
#define __MMU_INITIAL_MAPPING_PHYS_OFFSET 0
#define __MMU_INITIAL_MAPPING_VIRT_OFFSET 8
#define __MMU_INITIAL_MAPPING_SIZE_OFFSET 16
#define __MMU_INITIAL_MAPPING_FLAGS_OFFSET 24
#define __MMU_INITIAL_MAPPING_SIZE 40

// flags for initial mapping struct
#define MMU_INITIAL_MAPPING_TEMPORARY (0x1)
#define MMU_INITIAL_MAPPING_FLAG_UNCACHED (0x2)
#define MMU_INITIAL_MAPPING_FLAG_DEVICE (0x4)
#define MMU_INITIAL_MAPPING_FLAG_DYNAMIC (0x8) // entry has to be patched up by platform_reset

// this file can be included from assembly to get to the defines above
#ifndef ASSEMBLY

#include <magenta/compiler.h>
#include <sys/types.h>

struct mmu_initial_mapping {
    paddr_t phys;
    vaddr_t virt;
    size_t size;
    unsigned int flags;
    const char* name;
};

// Assert that the assembly macros above match this struct.
static_assert(__offsetof(struct mmu_initial_mapping, phys) == __MMU_INITIAL_MAPPING_PHYS_OFFSET, "");
static_assert(__offsetof(struct mmu_initial_mapping, virt) == __MMU_INITIAL_MAPPING_VIRT_OFFSET, "");
static_assert(__offsetof(struct mmu_initial_mapping, size) == __MMU_INITIAL_MAPPING_SIZE_OFFSET, "");
static_assert(__offsetof(struct mmu_initial_mapping, flags) == __MMU_INITIAL_MAPPING_FLAGS_OFFSET, "");
static_assert(sizeof(struct mmu_initial_mapping) == __MMU_INITIAL_MAPPING_SIZE, "");

// Platform or target must fill out one of these to set up the initial memory map
// for kernel and enough IO space to boot.
// Declared extern C because it may be accessed from assembly.
extern "C" struct mmu_initial_mapping mmu_initial_mappings[];

#endif // !ASSEMBLY
