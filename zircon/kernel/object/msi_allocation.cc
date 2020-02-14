// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/zircon-internal/thread_annotations.h>
#include <sys/types.h>

#include <fbl/ref_counted.h>
#include <ktl/move.h>
#include <object/msi_allocation.h>

#include "lib/counters.h"

KCOUNTER(msi_create_count, "msi.create")
KCOUNTER(msi_destroy_count, "msi.destroy")

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

  zx_status_t st = msi_alloc_fn(irq_cnt, false /* can_target_64bit */, false /* is_msix */, &block);
  if (st != ZX_OK) {
    return st;
  }

  std::array<char, ZX_MAX_NAME_LEN> name;
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
  info->num_irq = block_.num_irq;
}
