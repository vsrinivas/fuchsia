// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/mmu.h>
#include <arch/arm64/periphmap.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>

#define PERIPH_RANGE_MAX    4

typedef struct {
    uint64_t base_phys;
    uint64_t base_virt;
    uint64_t length;
} periph_range_t;

static periph_range_t periph_ranges[PERIPH_RANGE_MAX] = {};

zx_status_t add_periph_range(paddr_t base_phys, size_t length) {
    // peripheral ranges are allocated below KERNEL_BASE
     uint64_t base_virt = KERNEL_BASE;

    DEBUG_ASSERT(IS_PAGE_ALIGNED(base_phys));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(length));

    for (auto& range : periph_ranges) {
        if (range.length == 0) {
            base_virt -= length;
            auto status = arm64_boot_map_v(base_virt, base_phys, length, MMU_INITIAL_MAP_DEVICE);
            if (status == ZX_OK) {
                range.base_phys = base_phys;
                range.base_virt = base_virt;
                range.length = length;
            }
            return status;
        } else {
            base_virt -= range.length;
        }
    }
    return ZX_ERR_OUT_OF_RANGE;
}

void reserve_periph_ranges() {
    for (auto& range : periph_ranges) {
        if (range.length == 0) {
            break;
        }
        VmAspace::kernel_aspace()->ReserveSpace("periph", range.length, range.base_virt);
    }
}

vaddr_t periph_paddr_to_vaddr(paddr_t paddr) {
    for (auto& range : periph_ranges) {
        if (range.length == 0) {
            break;
        } else if (paddr >= range.base_phys) {
            uint64_t offset = paddr - range.base_phys;
            if (offset < range.length) {
                return range.base_virt + offset;
            }
        }
    }
    return 0;
}
