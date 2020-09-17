// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/counters.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <pow2.h>
#include <sys/types.h>
#include <trace.h>

#include <fbl/ref_counted.h>
#include <ktl/move.h>
#include <ktl/popcount.h>
#include <object/msi_allocation.h>

KCOUNTER(msi_create_count, "msi.create")
KCOUNTER(msi_destroy_count, "msi.destroy")

#define LOCAL_TRACE 0

zx_status_t MsiAllocation::Create(uint32_t irq_cnt, fbl::RefPtr<MsiAllocation>* obj,
                                  MsiAllocFn msi_alloc_fn, MsiFreeFn msi_free_fn,
                                  MsiSupportedFn msi_support_fn,
                                  ResourceDispatcher::ResourceStorage* rsrc_storage) {
  if (!msi_support_fn()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  msi_block_t block = {};
  auto cleanup = fbl::MakeAutoCall([&msi_free_fn, &block]() {
    if (block.allocated) {
      msi_free_fn(&block);
    }
  });

  // Ensure the requested IRQs fit within the mask of permitted IRQs in an
  // allocation. MSI allocations must be a power of two.
  // MSI supports up to 32, MSI-X supports up to 2048.
  if (irq_cnt == 0 || irq_cnt > kMsiAllocationCountMax || !ispow2(irq_cnt)) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t st = msi_alloc_fn(irq_cnt, false /* can_target_64bit */, false /* is_msix */, &block);
  if (st != ZX_OK) {
    return st;
  }

  LTRACEF("MSI Allocation: { tgr_addr = 0x%lx, tgt_data = 0x%08x, base_irq_id = %u }\n",
          block.tgt_addr, block.tgt_data, block.base_irq_id);

  ktl::array<char, ZX_MAX_NAME_LEN> name;
  if (block.num_irq == 1) {
    snprintf(name.data(), name.max_size(), "MSI vector %u", block.base_irq_id);
  } else {
    snprintf(name.data(), name.max_size(), "MSI vectors %u-%u", block.base_irq_id,
             block.base_irq_id + block.num_irq - 1);
  }

  KernelHandle<ResourceDispatcher> kres;
  zx_rights_t rights;
  // We've allocated a block of IRQs from the InterruptManager/GIC and now need
  // to ensure they're exclusively reserved at the resource level. The
  // ResourceDispatcher has its own mutex so we need to temporarily release the
  // spinlock. This is fine because at this point Create() hasn't returned this
  // MsiAllocation to a caller.
  st = ResourceDispatcher::Create(&kres, &rights, ZX_RSRC_KIND_IRQ, block.base_irq_id,
                                  block.num_irq, ZX_RSRC_FLAG_EXCLUSIVE, name.data(), rsrc_storage);
  if (st != ZX_OK) {
    return st;
  }

  fbl::AllocChecker ac;
  auto msi =
      fbl::AdoptRef<MsiAllocation>(new (&ac) MsiAllocation(kres.release(), block, msi_free_fn));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  kcounter_add(msi_create_count, 1);
  cleanup.cancel();
  *obj = ktl::move(msi);
  return ZX_OK;
}

zx_status_t MsiAllocation::ReserveId(MsiId msi_id) {
  Guard<SpinLock, IrqSave> guard{&lock_};
  if (msi_id >= block_.num_irq) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto id_mask = (1u << msi_id);
  if (ids_in_use_ & id_mask) {
    return ZX_ERR_ALREADY_BOUND;
  }

  ids_in_use_ |= id_mask;
  return ZX_OK;
}

zx_status_t MsiAllocation::ReleaseId(MsiId msi_id) {
  Guard<SpinLock, IrqSave> guard{&lock_};
  if (msi_id >= block_.num_irq) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto id_mask = (1u << msi_id);
  if (!(ids_in_use_ & id_mask)) {
    return ZX_ERR_BAD_STATE;
  }

  ids_in_use_ &= ~id_mask;
  return ZX_OK;
}

MsiAllocation::~MsiAllocation() {
  Guard<SpinLock, IrqSave> guard{&lock_};
  if (block_.allocated) {
    msi_free_fn_(&block_);
  }
  DEBUG_ASSERT(!block_.allocated);
  kcounter_add(msi_destroy_count, 1);
}

void MsiAllocation::GetInfo(zx_info_msi* info) const TA_EXCL(lock_) {
  DEBUG_ASSERT(info);
  Guard<SpinLock, IrqSave> guard{&lock_};
  info->target_addr = block_.tgt_addr;
  info->target_data = block_.tgt_data;
  info->base_irq_id = block_.base_irq_id;
  info->interrupt_count = ktl::popcount(ids_in_use_);
  info->num_irq = block_.num_irq;
}
