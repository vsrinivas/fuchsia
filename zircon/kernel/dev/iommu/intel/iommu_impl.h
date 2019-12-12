// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_IOMMU_INTEL_IOMMU_IMPL_H_
#define ZIRCON_KERNEL_DEV_IOMMU_INTEL_IOMMU_IMPL_H_

#include <bits.h>
#include <zircon/syscalls/iommu.h>

#include <dev/interrupt.h>
#include <dev/iommu.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <hwreg/mmio.h>
#include <kernel/lockdep.h>
#include <kernel/mutex.h>

#include "domain_allocator.h"
#include "hw.h"
#include "iommu_page.h"

class VmMapping;

namespace intel_iommu {

class ContextTableState;
class DeviceContext;

class IommuImpl final : public Iommu {
 public:
  static zx_status_t Create(ktl::unique_ptr<const uint8_t[]> desc, size_t desc_len,
                            fbl::RefPtr<Iommu>* out);

  bool IsValidBusTxnId(uint64_t bus_txn_id) const final;

  zx_status_t Map(uint64_t bus_txn_id, const fbl::RefPtr<VmObject>& vmo, uint64_t offset,
                  size_t size, uint32_t perms, dev_vaddr_t* vaddr, size_t* mapped_len) final;
  zx_status_t MapContiguous(uint64_t bus_txn_id, const fbl::RefPtr<VmObject>& vmo, uint64_t offset,
                            size_t size, uint32_t perms, dev_vaddr_t* vaddr,
                            size_t* mapped_len) final;
  zx_status_t Unmap(uint64_t bus_txn_id, dev_vaddr_t vaddr, size_t size) final;

  zx_status_t ClearMappingsForBusTxnId(uint64_t bus_txn_id) final;

  uint64_t minimum_contiguity(uint64_t bus_txn_id) final;
  uint64_t aspace_size(uint64_t bus_txn_id) final;

  ~IommuImpl() final;

  // TODO(teisenbe): These should be const, but need to teach the register
  // library about constness
  reg::Capability* caps() { return &caps_; }
  reg::ExtendedCapability* extended_caps() { return &extended_caps_; }

  // Invalidate all context cache entries
  void InvalidateContextCacheGlobal();
  // Invalidate all context cache entries that are in the specified domain
  void InvalidateContextCacheDomain(uint32_t domain_id);

  // Invalidate all IOTLB entries for all domains
  void InvalidateIotlbGlobal();
  // Invalidate all IOTLB entries for the specified domain
  void InvalidateIotlbDomainAll(uint32_t domain_id);
  void InvalidateIotlbDomainAllLocked(uint32_t domain_id) TA_REQ(lock_);

  // Invalidate the IOTLB entries for the specified translations.
  // |pages_pow2| indicates how many pages should be invalidated (calculated
  // as 2^|pages_pow2|).
  void InvalidateIotlbPageLocked(uint32_t domain_id, dev_vaddr_t vaddr, uint pages_pow2)
      TA_REQ(lock_);

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(IommuImpl);
  IommuImpl(volatile void* register_base, ktl::unique_ptr<const uint8_t[]> desc, size_t desc_len);

  static ds::Bdf decode_bus_txn_id(uint64_t bus_txn_id) {
    ds::Bdf bdf;
    bdf.set_bus(static_cast<uint16_t>(BITS_SHIFT(bus_txn_id, 15, 8)));
    bdf.set_dev(static_cast<uint16_t>(BITS_SHIFT(bus_txn_id, 7, 3)));
    bdf.set_func(static_cast<uint16_t>(BITS_SHIFT(bus_txn_id, 2, 0)));
    return bdf;
  }

  static zx_status_t ValidateIommuDesc(const ktl::unique_ptr<const uint8_t[]>& desc,
                                       size_t desc_len);

  // Set up initial root structures and enable translation
  zx_status_t Initialize();

  // Context cache invalidation
  void InvalidateContextCacheGlobalLocked() TA_REQ(lock_);
  void InvalidateContextCacheDomainLocked(uint32_t domain_id) TA_REQ(lock_);

  // IOTLB invalidation
  void InvalidateIotlbGlobalLocked() TA_REQ(lock_);

  zx_status_t SetRootTablePointerLocked(paddr_t pa) TA_REQ(lock_);
  zx_status_t SetTranslationEnableLocked(bool enabled, zx_time_t deadline) TA_REQ(lock_);
  zx_status_t ConfigureFaultEventInterruptLocked() TA_REQ(lock_);

  // Process Reserved Memory Mapping Regions and set them up as pass-through.
  zx_status_t EnableBiosReservedMappingsLocked() TA_REQ(lock_);

  void DisableFaultsLocked() TA_REQ(lock_);
  static interrupt_eoi FaultHandler(void* ctx);
  zx_status_t GetOrCreateContextTableLocked(ds::Bdf bdf, ContextTableState** tbl) TA_REQ(lock_);
  zx_status_t GetOrCreateDeviceContextLocked(ds::Bdf bdf, DeviceContext** context) TA_REQ(lock_);

  // Utility for waiting until a register field changes to a value, timing out
  // if the deadline elapses.  If deadline is ZX_TIME_INFINITE, then will never time
  // out.  Can only return NO_ERROR and ERR_TIMED_OUT.
  template <class RegType>
  zx_status_t WaitForValueLocked(RegType* reg,
                                 typename RegType::ValueType (RegType::*getter)() const,
                                 typename RegType::ValueType value, zx_time_t deadline)
      TA_REQ(lock_);

  volatile ds::RootTable* root_table() const TA_REQ(lock_) {
    return reinterpret_cast<volatile ds::RootTable*>(root_table_page_.vaddr());
  }

  DECLARE_MUTEX(IommuImpl) lock_;

  // Descriptor of this hardware unit
  ktl::unique_ptr<const uint8_t[]> desc_;
  size_t desc_len_;

  // Location of the memory-mapped hardware register bank.
  hwreg::RegisterIo mmio_ TA_GUARDED(lock_);

  // Interrupt allocation
  msi_block_t irq_block_ TA_GUARDED(lock_);

  // In-memory root table
  IommuPage root_table_page_ TA_GUARDED(lock_);
  // List of allocated context tables
  fbl::DoublyLinkedList<ktl::unique_ptr<ContextTableState>> context_tables_ TA_GUARDED(lock_);

  DomainAllocator domain_allocator_ TA_GUARDED(lock_);

  // A mask with bits set for each usable bit in an address with the largest allowed
  // address width.  E.g., if the largest allowed width is 48-bit,
  // max_guest_addr_mask will be 0xffff_ffff_ffff.
  uint64_t max_guest_addr_mask_ TA_GUARDED(lock_) = 0;
  uint32_t valid_pasid_mask_ TA_GUARDED(lock_) = 0;
  uint32_t iotlb_reg_offset_ TA_GUARDED(lock_) = 0;
  uint32_t fault_recording_reg_offset_ TA_GUARDED(lock_) = 0;
  uint32_t num_fault_recording_reg_ TA_GUARDED(lock_) = 0;
  bool supports_extended_context_ TA_GUARDED(lock_) = 0;

  reg::Capability caps_;
  reg::ExtendedCapability extended_caps_;
};

}  // namespace intel_iommu

#endif  // ZIRCON_KERNEL_DEV_IOMMU_INTEL_IOMMU_IMPL_H_
