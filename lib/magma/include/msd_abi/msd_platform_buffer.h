// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MSD_PLATFORM_BUFFER_H_
#define _MSD_PLATFORM_BUFFER_H_

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct msd_platform_buffer {
    uint32_t magic_;
};

// Functions here that have int32_t return values use 0 to indicate success

int32_t msd_platform_buffer_alloc(struct msd_platform_buffer** buffer_out, uint64_t size,
                                  uint64_t* size_out, uint32_t* handle_out);

void msd_platform_buffer_incref(struct msd_platform_buffer* buffer);

void msd_platform_buffer_decref(struct msd_platform_buffer* buffer);

uint32_t msd_platform_buffer_getref(struct msd_platform_buffer* buffer);

int32_t msd_platform_buffer_get_size(struct msd_platform_buffer* buffer, uint64_t* size_out);

int32_t msd_platform_buffer_get_handle(struct msd_platform_buffer* buffer, uint32_t* handle_out);

// Returns a cpu virtual address for the entire buffer.
int32_t msd_platform_buffer_map_cpu(struct msd_platform_buffer* buffer, void** addr_out);

// Releases the cpu virtual mapping for the buffer.
int32_t msd_platform_buffer_unmap_cpu(struct msd_platform_buffer* buffer);

// Ensures that the buffer's backing store is physically resident.
// May be called multiple times.
int32_t msd_platform_buffer_pin_pages(struct msd_platform_buffer* buffer);

// Releases a corresponding call to pin.
int32_t msd_platform_buffer_unpin_pages(struct msd_platform_buffer* buffer);

// Returns success and the number of pinned pages if the buffer is pinned.
int32_t msd_platform_buffer_pinned_page_count(struct msd_platform_buffer* buffer,
                                              uint32_t* num_pages_out);

// Returns a cpu virtual address for the given page.
int32_t msd_platform_buffer_map_page_cpu(struct msd_platform_buffer* buffer, uint32_t page_index,
                                         void** addr_out);

// Releases any cpu virtual mapping for the given page.
int32_t msd_platform_buffer_unmap_page_cpu(struct msd_platform_buffer* buffer, uint32_t page_index);

// Returns a bus address for the given page (physical address or iommu mapped address).
int32_t msd_platform_buffer_map_page_bus(struct msd_platform_buffer* buffer, uint32_t page_index,
                                         uint64_t* addr_out);

// Releases any bus address mapping for the given page.
int32_t msd_platform_buffer_unmap_page_bus(struct msd_platform_buffer* buffer, uint32_t page_index);

#if defined(__cplusplus)
}
#endif

#endif // _MSD_PLATFORM_BUFFER_H_
