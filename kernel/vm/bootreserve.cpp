// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <sys/types.h>

#include <vm/bootreserve.h>

#include "vm_priv.h"

#include <trace.h>
#include <vm/pmm.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

static const size_t NUM_RESERVES = 16;
static reserve_range_t res[NUM_RESERVES];
static size_t res_idx;

void boot_reserve_init() {
    /* add the kernel to the boot reserve list */
    boot_reserve_add_range(get_kernel_base_phys(), get_kernel_size());
}

zx_status_t boot_reserve_add_range(paddr_t pa, size_t len) {
    dprintf(INFO, "PMM: boot reserve add [%#" PRIxPTR ", %#" PRIxPTR "]\n", pa, pa + len - 1);

    if (res_idx == NUM_RESERVES) {
        panic("too many boot reservations\n");
    }

    // insert into the list, sorted
    paddr_t end = pa + len - 1;
    DEBUG_ASSERT(end > pa);
    for (size_t i = 0; i < res_idx; i++) {
        if (Intersects(res[i].pa, res[i].len, pa, len)) {
            // we have a problem that we are not equipped to handle right now
            panic("boot_reserve_add_range: pa %#" PRIxPTR " len %zx intersects existing range\n",
                  pa, len);
        }

        if (res[i].pa > end) {
            // insert before this one
            memmove(&res[i + 1], &res[i], (res_idx - i) * sizeof(res[0]));
            res[i].pa = pa;
            res[i].len = len;
            res_idx++;
            return ZX_OK;
        }
    }

    // insert it at the end
    res[res_idx].pa = pa;
    res[res_idx].len = len;
    res_idx++;
    return ZX_OK;
}

// iterate through the reserved ranges and mark them as WIRED in the pmm
void boot_reserve_wire() {
    static list_node reserved_page_list = LIST_INITIAL_VALUE(reserved_page_list);

    for (size_t i = 0; i < res_idx; i++) {
        dprintf(INFO, "PMM: boot reserve marking WIRED [%#" PRIxPTR ", %#" PRIxPTR "]\n",
                res[i].pa, res[i].pa + res[i].len - 1);

        size_t pages = ROUNDUP_PAGE_SIZE(res[i].len) / PAGE_SIZE;
        size_t actual = pmm_alloc_range(res[i].pa, pages, &reserved_page_list);
        if (actual != pages) {
            printf("PMM: unable to reserve reserved range [%#" PRIxPTR ", %#" PRIxPTR "]\n",
                   res[i].pa, res[i].pa + res[i].len - 1);
            continue; // this is probably fatal but go ahead and continue
        }
    }

    // mark all of the pages we allocated as WIRED
    vm_page_t* p;
    list_for_every_entry (&reserved_page_list, p, vm_page_t, free.node) {
        p->state = VM_PAGE_STATE_WIRED;
    }
}

static paddr_t upper_align(paddr_t range_pa, size_t range_len, size_t len) {
    return (range_pa + range_len - len);
}

zx_status_t boot_reserve_range_search(paddr_t range_pa, size_t range_len, size_t alloc_len,
                                      reserve_range_t* alloc_range) {
    LTRACEF("range pa %#" PRIxPTR " len %#zx alloc_len %#zx\n", range_pa, range_len, alloc_len);

    paddr_t alloc_pa = upper_align(range_pa, range_len, alloc_len);

retry:
    // see if it intersects any reserved range
    LTRACEF("trying alloc range %#" PRIxPTR " len %#zx\n", alloc_pa, alloc_len);
    for (size_t i = 0; i < res_idx; i++) {
        if (Intersects(res[i].pa, res[i].len, alloc_pa, alloc_len)) {
            // it intersects this range, move the search spot back to just before it and try again
            LTRACEF("alloc range %#" PRIxPTR " len %zx intersects with reserve range\n", alloc_pa, alloc_len);
            alloc_pa = res[i].pa - alloc_len;

            LTRACEF("moving and retrying at %#" PRIxPTR "\n", alloc_pa);

            // make sure this still works with our input constraints
            if (alloc_pa < range_pa) {
                LTRACEF("failed to allocate\n");
                return ZX_ERR_NO_MEMORY;
            }

            goto retry;
        }
    }

    // fell off the list without retrying, must have suceeded
    LTRACEF("returning [%#" PRIxPTR ", %#" PRIxPTR "]\n",
            alloc_pa, alloc_pa + alloc_len - 1);

    *alloc_range = {alloc_pa, alloc_len};
    return ZX_OK;
}
