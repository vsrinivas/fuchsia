// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/phys-iter.h>
#include <sys/param.h>
#include <zircon/assert.h>

#include <string.h>

void phys_iter_init(phys_iter_t* iter, phys_iter_buffer_t* buf, size_t max_length) {
    memcpy(&iter->buf, buf, sizeof(phys_iter_buffer_t));
    iter->offset = 0;
    ZX_DEBUG_ASSERT(max_length % PAGE_SIZE == 0);
    if (max_length == 0) {
        max_length = UINT64_MAX;
    }
    iter->max_length = max_length;
    // iter->page is index of page containing buf->vmo_offset,
    // and iter->last_page is index of page containing buf->vmo_offset + buf->length
    iter->page = 0;
    if (buf->length > 0) {
        size_t align_adjust = buf->vmo_offset & (PAGE_SIZE - 1);
        iter->last_page = (buf->length + align_adjust - 1) / PAGE_SIZE;
    } else {
        iter->last_page = 0;
    }
}

size_t phys_iter_next(phys_iter_t* iter, zx_paddr_t* out_paddr) {
    phys_iter_buffer_t buf = iter->buf;
    zx_off_t offset = iter->offset;
    size_t max_length = iter->max_length;
    size_t length = buf.length;
    if (offset >= length) {
        return 0;
    }
    size_t remaining = length - offset;
    zx_paddr_t* phys_addrs = buf.phys;
    size_t align_adjust = buf.vmo_offset & (PAGE_SIZE - 1);
    zx_paddr_t phys = phys_addrs[iter->page];
    size_t return_length = 0;

    if (buf.phys_count == 1) {
        // simple contiguous case
        *out_paddr = phys_addrs[0] + offset + align_adjust;
        return_length = remaining;
        if (return_length > max_length) {
            // end on a page boundary
            return_length = max_length - align_adjust;
        }
        iter->offset += return_length;
        return return_length;
    }

    if (offset == 0 && align_adjust > 0) {
        // if vmo_offset is unaligned we need to adjust out_paddr, accumulate partial page length
        // in return_length and skip to next page.
        // we will make sure the range ends on a page boundary so we don't need to worry about
        // alignment for subsequent iterations.
        *out_paddr = phys + align_adjust;
        return_length = MIN(PAGE_SIZE - align_adjust, remaining);
        remaining -= return_length;
        iter->page = 1;

        if (iter->page > iter->last_page || phys + PAGE_SIZE != phys_addrs[iter->page]) {
            iter->offset += return_length;
            return return_length;
        }
        phys = phys_addrs[iter->page];
    } else {
        *out_paddr = phys;
    }

    // below is more complicated case where we need to watch for discontinuities
    // in the physical address space.

    // loop through physical addresses looking for discontinuities
    while (remaining > 0 && iter->page <= iter->last_page) {
        const size_t increment = MIN(PAGE_SIZE, remaining);
        if (return_length + increment > max_length) {
            break;
        }
        return_length += increment;
        remaining -= increment;
        iter->page++;

        if (iter->page > iter->last_page) {
            break;
        }

        zx_paddr_t next = phys_addrs[iter->page];
        if (phys + PAGE_SIZE != next) {
            break;
        }
        phys = next;
    }

    if (return_length > max_length) {
        return_length = max_length;
    }
    iter->offset += return_length;
    return return_length;
}
