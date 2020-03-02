// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_MSI_ALLOCATION_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_MSI_ALLOCATION_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <sys/types.h>
#include <zircon/rights.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <dev/interrupt.h>
#include <fbl/ref_counted.h>
#include <kernel/spinlock.h>
#include <ktl/limits.h>
#include <ktl/move.h>
#include <object/resource_dispatcher.h>

// An MsiAllocation is a wrapper around an allocated block oVf MSI interrupts.
// It allows for multiple MsiDispatchers to share an allocated block, and
// synchronize access to an MSI capability dealing with multiple IRQs.
//
// By default, all MSI Allocations use the platform's kernel msi_*
// implementation for management of MSI blocks, but tests can override the
// interface via Create() parameters. Since those methods are used in allocation
// of interrupts but not dispatch the indirection of those calls is an
// acceptable cost to have the benefit of not making the type signature more
// complex with other compile-time approaches.
class MsiAllocation : public fbl::RefCounted<MsiAllocation> {
 public:
  using MsiAllocFn = zx_status_t (*)(uint32_t, bool, bool, msi_block_t*);
  using MsiFreeFn = void (*)(msi_block_t*);
  using MsiSupportedFn = bool (*)();
  // For now limit the max number of allocations in a block to the limit of standard
  // MSI. MSI-X's enhanced allocation limits are not going to come into play until
  // we move interrupt allocation off of the bootstrap CPU.
  using IdBitMaskType = uint32_t;
  using MsiId = uint32_t;
  static constexpr uint32_t kMsiAllocationCountMax = ktl::numeric_limits<IdBitMaskType>::digits;

  static zx_status_t Create(uint32_t irq_cnt, fbl::RefPtr<MsiAllocation>* obj,
                            // Defaults to allow test mocks to override.
                            MsiAllocFn msi_alloc_fn = msi_alloc_block,
                            MsiFreeFn msi_free_fn = msi_free_block,
                            MsiSupportedFn msi_support_fn = msi_is_supported,
                            ResourceDispatcher::ResourceStorage* rsrc_storage = nullptr);

  ~MsiAllocation();

  void GetInfo(zx_info_msi* info) const TA_EXCL(lock_);

  static zx_obj_type_t get_type() { return ZX_OBJ_TYPE_MSI_ALLOCATION; }
  const msi_block_t& block() const TA_REQ(lock_) { return block_; }
  // Interface for MsiDispatchers to reserve a given MSI id for management.
  zx_status_t ReserveId(MsiId msi_id) TA_EXCL(lock_);
  zx_status_t ReleaseId(MsiId msi_id) TA_EXCL(lock_);

  Lock<SpinLock>& lock() TA_EXCL(lock_) TA_RET_CAP(lock_) { return lock_; }

 private:
  explicit MsiAllocation(fbl::RefPtr<ResourceDispatcher>&& resource, const msi_block_t block,
                         MsiFreeFn msi_free_fn)
      : resource_(ktl::move(resource)), msi_free_fn_(msi_free_fn), block_(block) {}

  // MsiAllocationInterface abstraction methods to allow mocking but not add complexity
  // to users of the object via requiring templates.
  fbl::RefPtr<ResourceDispatcher> resource_;
  // A pointer to the function to free the block when the object is released.
  MsiFreeFn msi_free_fn_;
  // Function pointers for MSI platform functions to facilitate unit tests.
  msi_block_t block_ TA_GUARDED(lock_) = {};
  // A bitfield of MSI ids currently associated with MsiDispatchers.
  IdBitMaskType ids_in_use_ TA_GUARDED(lock_) = 0;
  // Used to synchronize access to an MSI vector control register for
  // MSI blocks that consist of multiple vectors and MsiInterruptDispatchers.
  // It is not used to protect the MsiAllocation itself.
  mutable DECLARE_SPINLOCK(MsiAllocation) lock_;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_MSI_ALLOCATION_H_
