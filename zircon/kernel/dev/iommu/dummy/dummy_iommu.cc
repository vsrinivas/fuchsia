// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <err.h>

#include <new>

#include <dev/iommu/dummy.h>
#include <fbl/ref_ptr.h>
#include <ktl/algorithm.h>
#include <ktl/move.h>
#include <vm/vm.h>

#define INVALID_PADDR UINT64_MAX

DummyIommu::DummyIommu() {}

zx_status_t DummyIommu::Create(ktl::unique_ptr<const uint8_t[]> desc, size_t desc_len,
                               fbl::RefPtr<Iommu>* out) {
  if (desc_len != sizeof(zx_iommu_desc_dummy_t)) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  auto instance = fbl::AdoptRef<DummyIommu>(new (&ac) DummyIommu());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  *out = ktl::move(instance);
  return ZX_OK;
}

DummyIommu::~DummyIommu() {}

bool DummyIommu::IsValidBusTxnId(uint64_t bus_txn_id) const { return true; }

zx_status_t DummyIommu::Map(uint64_t bus_txn_id, const fbl::RefPtr<VmObject>& vmo, uint64_t offset,
                            size_t size, uint32_t perms, dev_vaddr_t* vaddr, size_t* mapped_len) {
  DEBUG_ASSERT(vaddr);
  DEBUG_ASSERT(mapped_len);

  if (!IS_PAGE_ALIGNED(offset) || size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (perms & ~(IOMMU_FLAG_PERM_READ | IOMMU_FLAG_PERM_WRITE | IOMMU_FLAG_PERM_EXECUTE)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (perms == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  paddr_t paddr = INVALID_PADDR;
  size = ROUNDUP(size, PAGE_SIZE);
  zx_status_t status = vmo->LookupContiguous(offset, size, &paddr);
  // If the range is fundamentally incorrect or out of range then we immediately error. Otherwise
  // even if we have some other error case we will fall back to attempting single pages at a time.
  if (status == ZX_ERR_INVALID_ARGS || status == ZX_ERR_OUT_OF_RANGE) {
    return status;
  }
  if (status == ZX_OK) {
    DEBUG_ASSERT(paddr != INVALID_PADDR);
    *vaddr = paddr;
    *mapped_len = size;
    return ZX_OK;
  }

  status = vmo->LookupContiguous(offset, PAGE_SIZE, &paddr);
  if (status != ZX_OK) {
    return status;
  }
  DEBUG_ASSERT(paddr != INVALID_PADDR);
  *vaddr = paddr;
  *mapped_len = PAGE_SIZE;
  return ZX_OK;
}

zx_status_t DummyIommu::MapContiguous(uint64_t bus_txn_id, const fbl::RefPtr<VmObject>& vmo,
                                      uint64_t offset, size_t size, uint32_t perms,
                                      dev_vaddr_t* vaddr, size_t* mapped_len) {
  DEBUG_ASSERT(vaddr);
  DEBUG_ASSERT(mapped_len);

  if (!IS_PAGE_ALIGNED(offset) || size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (perms & ~(IOMMU_FLAG_PERM_READ | IOMMU_FLAG_PERM_WRITE | IOMMU_FLAG_PERM_EXECUTE)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (perms == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  paddr_t paddr = INVALID_PADDR;
  zx_status_t status = vmo->LookupContiguous(offset, size, &paddr);
  if (status != ZX_OK) {
    return status;
  }
  DEBUG_ASSERT(paddr != INVALID_PADDR);

  *vaddr = paddr;
  *mapped_len = size;
  return ZX_OK;
}

zx_status_t DummyIommu::Unmap(uint64_t bus_txn_id, dev_vaddr_t vaddr, size_t size) {
  if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(size)) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t DummyIommu::ClearMappingsForBusTxnId(uint64_t bus_txn_id) { return ZX_OK; }

uint64_t DummyIommu::minimum_contiguity(uint64_t bus_txn_id) { return PAGE_SIZE; }

uint64_t DummyIommu::aspace_size(uint64_t bus_txn_id) { return UINT64_MAX; }
