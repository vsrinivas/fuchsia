// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <sys/param.h>
#include <zircon/assert.h>

#include <ddk/phys-iter.h>

// Returns the buffer offset of the start of the current segment being iterated over.
static size_t segment_offset(phys_iter_t* iter) {
  // The physical pages list begins with the page containing buf->vmo_offset,
  // while the stored segment_offset is relative to the buffer vmo offset.
  return (iter->buf.vmo_offset & (PAGE_SIZE - 1)) + iter->segment_offset;
}

// Initializes the iterator for the next segment to iterate over.
// Returns whether there was a next segment.
static bool init_next_sg_entry(phys_iter_t* iter) {
  // Finished iterating through the scatter gather list.
  if (iter->buf.sg_list && iter->next_sg_entry_idx >= iter->buf.sg_count) {
    return false;
  }
  // No scatter gather list was provided and we have finished iterating
  // over the requested length.
  if (!iter->buf.sg_list && iter->total_iterated == iter->buf.length) {
    return false;
  }
  if (iter->buf.sg_list) {
    const phys_iter_sg_entry_t* next = &iter->buf.sg_list[iter->next_sg_entry_idx];
    iter->segment_length = next->length;
    iter->segment_offset = next->offset;
    iter->next_sg_entry_idx++;
  } else {
    iter->segment_length = iter->buf.length;
    iter->segment_offset = 0;
  }

  iter->offset = 0;
  // iter->page is index of page containing the next segment start offset.
  // and iter->last_page is index of page containing the segment offset + segment->length
  iter->page = iter->buf.phys_count == 1 ? 0 : segment_offset(iter) / PAGE_SIZE;
  if (iter->segment_length > 0) {
    iter->last_page = (iter->segment_length + segment_offset(iter) - 1) / PAGE_SIZE;
    iter->last_page = MIN(iter->last_page, iter->buf.phys_count - 1);
  } else {
    iter->last_page = 0;
  }
  return true;
}

void phys_iter_init(phys_iter_t* iter, const phys_iter_buffer_t* buf, size_t max_length) {
  memset(iter, 0, sizeof(phys_iter_t));
  memcpy(&iter->buf, buf, sizeof(phys_iter_buffer_t));
  ZX_DEBUG_ASSERT(max_length % PAGE_SIZE == 0);
  if (max_length == 0) {
    max_length = UINT64_MAX;
  }
  iter->max_length = max_length;
  init_next_sg_entry(iter);
}

static inline void phys_iter_increment(phys_iter_t* iter, size_t len) {
  iter->total_iterated += len;
  iter->offset += len;
}

size_t phys_iter_next(phys_iter_t* iter, zx_paddr_t* out_paddr) {
  *out_paddr = 0;

  // Check if we've finished iterating over the current segment.
  // We shouldn't have any zero length segments, but use a while loop just in case.
  while (iter->offset >= iter->segment_length) {
    bool has_next = init_next_sg_entry(iter);
    if (!has_next) {
      return 0;
    }
  }

  if (iter->page >= iter->buf.phys_count) {
    return 0;
  }

  const phys_iter_buffer_t* buf = &iter->buf;
  const zx_off_t offset = iter->offset;
  const size_t max_length = iter->max_length;
  const size_t length = iter->segment_length;

  size_t remaining = length - offset;
  const zx_paddr_t* phys_addrs = buf->phys;
  size_t align_adjust = segment_offset(iter) & (PAGE_SIZE - 1);
  zx_paddr_t phys = phys_addrs[iter->page];
  size_t return_length = 0;

  if (buf->phys_count == 1) {
    // simple contiguous case
    *out_paddr = phys_addrs[0] + offset + segment_offset(iter);
    return_length = remaining;
    if (return_length > max_length) {
      // end on a page boundary
      return_length = max_length - align_adjust;
    }
    phys_iter_increment(iter, return_length);
    return return_length;
  }

  if (offset == 0 && align_adjust > 0) {
    // if the segment offset is unaligned we need to adjust out_paddr, accumulate partial
    // page length in return_length and skip to next page.
    // we will make sure the range ends on a page boundary so we don't need to worry about
    // alignment for subsequent iterations.
    *out_paddr = phys + align_adjust;
    return_length = MIN(PAGE_SIZE - align_adjust, remaining);
    remaining -= return_length;
    iter->page++;

    if (iter->page > iter->last_page || phys + PAGE_SIZE != phys_addrs[iter->page]) {
      phys_iter_increment(iter, return_length);
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
  phys_iter_increment(iter, return_length);
  return return_length;
}
