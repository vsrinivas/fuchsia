// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_IOMMU_DUMMY_INCLUDE_DEV_IOMMU_DUMMY_H_
#define ZIRCON_KERNEL_DEV_IOMMU_DUMMY_INCLUDE_DEV_IOMMU_DUMMY_H_

#include <zircon/compiler.h>
#include <zircon/syscalls/iommu.h>

#include <dev/iommu.h>
#include <ktl/unique_ptr.h>

class DummyIommu final : public Iommu {
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

  ~DummyIommu() final;

  DISALLOW_COPY_ASSIGN_AND_MOVE(DummyIommu);

 private:
  DummyIommu();
};

#endif  // ZIRCON_KERNEL_DEV_IOMMU_DUMMY_INCLUDE_DEV_IOMMU_DUMMY_H_
