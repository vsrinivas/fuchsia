// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

__BEGIN_CDECLS;

// An entry in a scatter gather list.
typedef struct {
    // length starting at the scatter gather entry offset, must be non zero
    size_t length;
    // offset relative to the buffer's vmo_offset
    size_t offset;
} phys_iter_sg_entry_t;

// Specifies the buffer to iterate over.
typedef struct {
    zx_paddr_t* phys;     // list of physical addresses backing the buffer starting at vmo_offset
    uint64_t phys_count;  // number of entries in phys list

    size_t length;        // length of the buffer starting at vmo_offset, used if scatter gather
                          // list is not present
    uint64_t vmo_offset;  // offset into first page to start iterating on

    phys_iter_sg_entry_t* sg_list;  // optional list of scatter gather entries to iterate over
    size_t sg_count;                // number of entries in the scatter gather list
} phys_iter_buffer_t;

// Used to iterate over contiguous buffer ranges in the physical address space.
typedef struct {
    phys_iter_buffer_t buf;

    size_t      total_iterated;  // total bytes iterated across all calls for this iterator
    zx_off_t    offset;          // current offset in the segment (relative to the segment offset)
                                 // i.e. the total number of bytes iterated for the current segment
    size_t      max_length;      // max length to be returned by phys_iter_next()
    uint64_t    page;            // index of page in buf->phys that contains offset
    uint64_t    last_page;       // last valid page index in buf->phys

    size_t next_sg_entry_idx;    // next index in the scatter gather list
    size_t segment_offset;       // offset of the current scatter gather entry, relative to buffer
                                 // vmo_offset, or zero if no scatter gather list is present.
    size_t segment_length;       // length of the buffer for the current scatter gather entry,
                                 // or equal to buf.length if no scatter gather list is present.
} phys_iter_t;

// Initializes a phys_iter_t for iterating over physical memory.
// max_length is the maximum length of a range returned by phys_iter_next()
// max_length must be either a positive multiple of PAGE_SIZE, or zero for no limit.
void phys_iter_init(phys_iter_t* iter, phys_iter_buffer_t* buf, size_t max_length);

// Returns the next physical address and length for the iterator up to size max_length.
// Return value is length, or zero if iteration is done.
size_t phys_iter_next(phys_iter_t* iter, zx_paddr_t* out_paddr);

__END_CDECLS;
