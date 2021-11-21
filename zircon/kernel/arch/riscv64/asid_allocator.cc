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

AsidAllocator::AsidAllocator() {
  bitmap_.Reset(MMU_RISCV64_MAX_USER_ASID + 1);
}

AsidAllocator::~AsidAllocator() {}

zx::status<uint16_t> AsidAllocator::Alloc() {
  uint16_t new_asid;

  // use the bitmap allocator to allocate ids in the range of
  // [MMU_RISCV64_FIRST_USER_ASID, MMU_RISCV64_MAX_USER_ASID]
  // start the search from the last found id + 1 and wrap when hitting the end of the range
  {
    Guard<Mutex> al{&lock_};

    size_t val;
    bool notfound = bitmap_.Get(last_ + 1, MMU_RISCV64_MAX_USER_ASID + 1, &val);
    if (unlikely(notfound)) {
      // search again from the start
      notfound = bitmap_.Get(MMU_RISCV64_FIRST_USER_ASID, MMU_RISCV64_MAX_USER_ASID + 1, &val);
      if (unlikely(notfound)) {
        return zx::error(ZX_ERR_NO_MEMORY);
      }
    }
    bitmap_.SetOne(val);

    DEBUG_ASSERT(val <= UINT16_MAX);

    new_asid = (uint16_t)val;
    last_ = new_asid;
  }

  LTRACEF("new asid %#x\n", new_asid);

  return zx::ok(new_asid);
}

zx::status<> AsidAllocator::Free(uint16_t asid) {
  LTRACEF("free asid %#x\n", asid);

  Guard<Mutex> al{&lock_};

  bitmap_.ClearOne(asid);

  return zx::ok();
}
