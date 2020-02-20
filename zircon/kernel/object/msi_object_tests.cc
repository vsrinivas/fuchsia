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
#include <object/msi_dispatcher.h>
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

int interrupt_waiter(void* arg) {
  auto dispatcher = reinterpret_cast<InterruptDispatcher*>(arg);
  zx_time_t out;
  return (dispatcher->WaitForInterrupt(&out) == ZX_OK);
}

// Use a static var for tracking calls rather than a lambda to avoid storage issues with lambda
// captures and function pointers without having to increase complexity in the dispatcher.
static uint32_t register_call_count = 0;
void register_fn(const msi_block_t*, uint, int_handler, void*) { register_call_count++; }

static bool interrupt_creation_test() {
  BEGIN_TEST;
  ResourceDispatcher::ResourceStorage rsrc_storage;
  const uint32_t msi_cnt = 8;
  uint32_t msi_id = 3;
  const uint32_t reg_offset = 16;
  ASSERT_EQ(ZX_OK, ResourceDispatcher::InitializeAllocator(ZX_RSRC_KIND_IRQ, msi_id, kVectorMax,
                                                           &rsrc_storage));
  // Create an MsiAllocation and Interrupt that will attempt to use masking at the MSI Capability
  // level.
  fbl::RefPtr<MsiAllocation> alloc;
  ASSERT_EQ(ZX_OK, MsiAllocation::Create(msi_cnt, &alloc, MsiAllocate, MsiFree, MsiIsSupportedTrue,
                                         &rsrc_storage));
  ASSERT_EQ(1u, rsrc_storage.resource_list.size_slow());

  // This test emulates a block of MSI interrupts each taking up a given bit in a register for their
  // own masking. It validates that the MsiDispatcher masks / unmasks the correct bit, and that the
  // operation of the inherited InterruptDispatcher side of things behaves correctly when interrupts
  // are triggered.
  register_call_count = 0;
  for (uint32_t msi_id = 0; msi_id < msi_cnt; msi_id++) {
    zx_rights_t rights;
    fbl::RefPtr<VmObject> vmo, vmo_noncontig;
    size_t vmo_size = 48u;
    ASSERT_EQ(ZX_OK,
              VmObjectPaged::CreateContiguous(PMM_ALLOC_FLAG_ANY, vmo_size, 0 /* options */, &vmo));
    ASSERT_EQ(ZX_OK,
              VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0 /* options */, vmo_size, &vmo_noncontig));
    // This should fail because the VMO is non-contiguous.
    KernelHandle<InterruptDispatcher> interrupt;
    ASSERT_EQ(
        ZX_ERR_INVALID_ARGS,
        MsiDispatcher::Create(alloc, msi_id, vmo_noncontig, reg_offset, MSI_FLAG_HAS_PVM, &rights,
                              &interrupt, register_fn, true /* virtual interrupt */));
    // This should fail because the VMO has not had a cache policy set.
    ASSERT_EQ(ZX_ERR_INVALID_ARGS,
              MsiDispatcher::Create(alloc, msi_id, vmo, reg_offset, MSI_FLAG_HAS_PVM, &rights,
                                    &interrupt, register_fn, true /* virtual interrupt */));
    ASSERT_EQ(ZX_OK, vmo->SetMappingCachePolicy(ZX_CACHE_POLICY_UNCACHED_DEVICE));
    // Now Create() should succeed.
    ASSERT_EQ(ZX_OK,
              MsiDispatcher::Create(alloc, msi_id, vmo, reg_offset, MSI_FLAG_HAS_PVM, &rights,
                                    &interrupt, register_fn, true /* virtual interrupt */));
    // This mapping must be created after the MsiDispatcher because the VMO's
    // cache policy is set within Create().
    fbl::RefPtr<VmMapping> mapping;
    ASSERT_EQ(ZX_OK, VmAspace::kernel_aspace()->RootVmar()->CreateVmMapping(
                         0 /* offset */, vmo_size, 0 /* align_pow2 */, 0 /* vmar_flags */, vmo,
                         0 /* vmo offset */, ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE,
                         nullptr, &mapping));
    auto* reg_ptr = reinterpret_cast<uint32_t*>(mapping->base() + reg_offset);

    // What the register should look like when |msi_id| is presently masked.
    uint32_t msi_id_masked = (1u << msi_id);
    // The mask bit should be set from creation of the object.
    EXPECT_EQ(msi_id_masked, *reg_ptr);

    // Now have a child thread wait on the interrupt and report success back.
    auto thread =
        thread_create("msi_object_waiter", interrupt_waiter,
                      reinterpret_cast<void*>(interrupt.dispatcher().get()), DEFAULT_PRIORITY);
    thread_resume(thread);
    // Now that the child is waiting on the interrupt it should be unmasked.
    EXPECT_EQ(msi_id_masked, *reg_ptr);
    // Finally, trigger the interrupt, check for success, then ensure it was masked again.
    interrupt.dispatcher()->Trigger(current_time());
    int ret = 0;
    EXPECT_EQ(ZX_OK, thread_join(thread, &ret, current_time() + ZX_SEC(1)));
    EXPECT_EQ(1, ret);
    ASSERT_EQ(msi_id_masked, *reg_ptr);
  }

  // the register fn should be called once for registering and once for
  // unregistering when we created and destroyed the dispatcher. This is done
  // |msi_cnt| times.
  ASSERT_EQ(register_call_count, 2 * msi_cnt);

  END_TEST;
}

UNITTEST_START_TESTCASE(msi_object)
UNITTEST("simple test for creation / get_info", allocation_creation_and_info_test)
UNITTEST("test for msi platform support", allocation_support_test)
UNITTEST("test basic msi dispatcher operation", interrupt_creation_test)
UNITTEST_END_TESTCASE(msi_object, "msi", "Tests for MSI objects")
