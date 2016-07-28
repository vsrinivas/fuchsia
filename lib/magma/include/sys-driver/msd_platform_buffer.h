// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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

int32_t msd_platform_buffer_get_size(struct msd_platform_buffer* buffer, uint64_t* size_out);

int32_t msd_platform_buffer_get_handle(struct msd_platform_buffer* buffer, uint32_t* handle_out);

// Returns a cpu virtual address for the entire buffer.
int32_t msd_platform_buffer_map_cpu(struct msd_platform_buffer* buffer, void** addr_out);

// Releases the cpu virtual mapping for the buffer.
int32_t msd_platform_buffer_unmap_cpu(struct msd_platform_buffer* buffer);

// Ensures that the buffer's backing store is physically resident.
// May be called multiple times.
// Optionally receive the number of pages pinned.
int32_t msd_platform_buffer_pin_pages(struct msd_platform_buffer* buffer, uint32_t* num_pages_out);

// Releases a corresponding call to pin.
int32_t msd_platform_buffer_unpin_pages(struct msd_platform_buffer* buffer);

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
