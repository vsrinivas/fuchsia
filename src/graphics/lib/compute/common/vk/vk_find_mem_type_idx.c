// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include <stdlib.h>

//
//
//

#include "vk_find_mem_type_idx.h"
#include "common/macros.h"

//
//
//

static
uint32_t
vk_spn_ffs(uint32_t * lsb, uint32_t const mask)
{
#ifdef _MSC_VER
  return _BitScanForward((unsigned long *)lsb,mask); // returns 0 or 1
#else
  uint32_t const lsb_plus_1 = __builtin_ffs(mask);

  *lsb = lsb_plus_1 - 1;

  return lsb_plus_1; // returns [0,32]
#endif
}

//
//
//

uint32_t
vk_find_mem_type_idx(VkPhysicalDeviceMemoryProperties const * pdmp,
                     uint32_t                                 memoryTypeBits,
                     VkMemoryPropertyFlags            const   mpf)
{
  //
  // FIXME -- jump between indices in the memoryTypeBits mask
  //
  uint32_t lsb;

  while (vk_spn_ffs(&lsb,memoryTypeBits) != 0)
    {
      // clear it
      memoryTypeBits &= ~BITS_TO_MASK_MACRO(lsb+1);

      // otherwise, find first match...
      VkMemoryPropertyFlags const common =
        pdmp->memoryTypes[lsb].propertyFlags & mpf;

      if (common == mpf)
        return lsb;
    }

  return UINT32_MAX;
}

//
//
//
