// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "flush.h"

#include "common/vk/assert.h"

//
// Flush a noncoherent mapped ring
//
void
spinel_ring_flush(struct spinel_device_vk const * vk,
                  VkDeviceMemory                  ring_dm,
                  VkDeviceSize                    ring_dm_offset,
                  uint32_t                        ring_size,
                  uint32_t                        ring_head,
                  uint32_t                        ring_span,
                  VkDeviceSize                    ring_elem_size)
{
  uint32_t const last_max     = ring_head + ring_span;
  uint32_t const last_hi      = MIN_MACRO(uint32_t, last_max, ring_size);
  uint32_t const ring_span_hi = last_hi - ring_head;

  VkDeviceSize const head_size    = ring_dm_offset + ring_elem_size * ring_head;
  VkDeviceSize const last_hi_size = ring_dm_offset + ring_elem_size * last_hi;

  // clang-format off
  VkDeviceSize const head_size_rd    = ROUND_DOWN_POW2_MACRO(head_size, vk->limits.noncoherent_atom_size);
  VkDeviceSize const last_hi_size_ru = ROUND_UP_POW2_MACRO(last_hi_size, vk->limits.noncoherent_atom_size);
  // clang-format on

  VkMappedMemoryRange mmr[2];

  mmr[0].sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  mmr[0].pNext  = NULL;
  mmr[0].memory = ring_dm;
  mmr[0].offset = head_size_rd;
  mmr[0].size   = last_hi_size_ru - head_size_rd;

  if (ring_span <= ring_span_hi)
    {
      vk(FlushMappedMemoryRanges(vk->d, 1, mmr));
    }
  else
    {
      // clang-format off
      VkDeviceSize const ring_dm_offset_rd = ROUND_DOWN_POW2_MACRO(ring_dm_offset, vk->limits.noncoherent_atom_size);
      uint32_t const     ring_span_lo      = ring_span - ring_span_hi;
      VkDeviceSize const ring_span_size    = ring_dm_offset + ring_elem_size * ring_span_lo;
      VkDeviceSize const ring_span_size_ru = ROUND_UP_POW2_MACRO(ring_span_size, vk->limits.noncoherent_atom_size);
      // clang-format on

      mmr[1].sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      mmr[1].pNext  = NULL;
      mmr[1].memory = ring_dm;
      mmr[1].offset = ring_dm_offset_rd;
      mmr[1].size   = ring_span_size_ru - ring_dm_offset_rd;

      vk(FlushMappedMemoryRanges(vk->d, 2, mmr));
    }
}

//
//
//
