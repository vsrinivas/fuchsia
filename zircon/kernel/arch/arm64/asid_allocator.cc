// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "asid_allocator.h"

#include <debug.h>
#include <trace.h>
#include <zircon/types.h>

#define LOCAL_TRACE 0

zx_status_t AsidAllocator::Alloc(uint16_t* asid) {
  uint16_t new_asid;

  // use the bitmap allocator to allocate ids in the range of
  // [MMU_ARM64_FIRST_USER_ASID, MMU_ARM64_MAX_USER_ASID]
  // start the search from the last found id + 1 and wrap when hitting the end of the range
  {
    Guard<Mutex> al{&lock_};

    size_t val;
    bool notfound = bitmap_.Get(last_ + 1, MMU_ARM64_MAX_USER_ASID + 1, &val);
    if (unlikely(notfound)) {
      // search again from the start
      notfound = bitmap_.Get(MMU_ARM64_FIRST_USER_ASID, MMU_ARM64_MAX_USER_ASID + 1, &val);
      if (unlikely(notfound)) {
        TRACEF("ARM64: out of ASIDs\n");
        return ZX_ERR_NO_MEMORY;
      }
    }
    bitmap_.SetOne(val);

    DEBUG_ASSERT(val <= UINT16_MAX);

    new_asid = (uint16_t)val;
    last_ = new_asid;
  }

  LTRACEF("new asid %#x\n", new_asid);

  *asid = new_asid;

  return ZX_OK;
}

zx_status_t AsidAllocator::Free(uint16_t asid) {
  LTRACEF("free asid %#x\n", asid);

  Guard<Mutex> al{&lock_};

  bitmap_.ClearOne(asid);

  return ZX_OK;
}
