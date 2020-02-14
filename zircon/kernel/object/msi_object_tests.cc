// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <lib/unittest/unittest.h>
#include <platform.h>
#include <zircon/errors.h>
#include <zircon/time.h>

#include <utility>

#include <fbl/ref_ptr.h>
#include <kernel/thread.h>
#include <object/handle.h>
#include <object/msi_allocation.h>
#include <vm/pmm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>

namespace {
bool MsiIsSupportedTrue() { return true; }
zx_status_t MsiAllocate(uint requested_irqs, bool /*unused*/, bool /*unused*/,
                        msi_block_t* out_block) {
  out_block->allocated = true;
  out_block->base_irq_id = 128;
  out_block->num_irq = requested_irqs;
  out_block->tgt_addr = 0x1234u;
  out_block->tgt_addr = 0x4321u;
  out_block->platform_ctx = nullptr;
  return ZX_OK;
}

// These functions are used to verify we properly bail out if MSI isn't supported.
void MsiFree(msi_block_t* block) { block->allocated = false; }
bool MsiIsSupportedFalse() { return false; }
zx_status_t MsiAllocateAssert(uint32_t /* unused */, bool /* unused */, bool /* unused */,
                              msi_block_t* /* unused */) {
  assert(false);
  return ZX_ERR_NOT_SUPPORTED;
}

void MsiFreeAssert(msi_block_t* /* unused */) { assert(false); }
}  // namespace

const uint32_t kVectorMax = 256u;

static bool allocation_creation_and_info_test() {
  BEGIN_TEST;

  const uint32_t test_irq_cnt = 5;
  ResourceDispatcher::ResourceStorage rsrc_storage;
  ASSERT_EQ(ZX_OK, ResourceDispatcher::InitializeAllocator(ZX_RSRC_KIND_IRQ, 0, kVectorMax,
                                                           &rsrc_storage));
  fbl::RefPtr<MsiAllocation> alloc;
  ASSERT_EQ(ZX_OK, MsiAllocation::Create(test_irq_cnt, &alloc, MsiAllocate, MsiFree,
                                         MsiIsSupportedTrue, &rsrc_storage));
  ASSERT_EQ(1u, rsrc_storage.resource_list.size_slow());

  zx_info_msi_t info = {};
  alloc->GetInfo(&info);

  // Grab the lock and compare the block values and info values to both our test
  // data and info data.
  Guard<SpinLock, IrqSave> guard{&alloc->lock()};
  ASSERT_EQ(test_irq_cnt, alloc->block().num_irq);
  ASSERT_EQ(true, alloc->block().allocated);
  ASSERT_EQ(info.base_irq_id, alloc->block().base_irq_id);
  ASSERT_EQ(info.num_irq, alloc->block().num_irq);
  ASSERT_EQ(info.target_addr, alloc->block().tgt_addr);
  ASSERT_EQ(info.target_data, alloc->block().tgt_data);
  END_TEST;
}

static bool allocation_support_test() {
  BEGIN_TEST;
  ResourceDispatcher::ResourceStorage rsrc_storage;
  ASSERT_EQ(ZX_OK, ResourceDispatcher::InitializeAllocator(ZX_RSRC_KIND_IRQ, 0, kVectorMax,
                                                           &rsrc_storage));

  {
    fbl::RefPtr<MsiAllocation> alloc;
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED,
              MsiAllocation::Create(1, &alloc, MsiAllocateAssert, MsiFreeAssert,
                                    MsiIsSupportedFalse, &rsrc_storage));
    ASSERT_EQ(0u, rsrc_storage.resource_list.size_slow());
  }

  END_TEST;
}

UNITTEST_START_TESTCASE(msi_object)
UNITTEST("simple test for creation / get_info", allocation_creation_and_info_test)
UNITTEST("test for msi platform support", allocation_support_test)
UNITTEST_END_TESTCASE(msi_object, "msi", "Tests for MSI Allocations")
