// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/iommu/dummy.h>

#include <err.h>
#include <fbl/new.h>
#include <fbl/ref_ptr.h>
#include <vm/vm.h>

DummyIommu::DummyIommu() {
}

zx_status_t DummyIommu::Create(fbl::unique_ptr<const uint8_t[]> desc, uint32_t desc_len,
                               fbl::RefPtr<Iommu>* out) {
    if (desc_len != sizeof(zx_iommu_desc_dummy_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AllocChecker ac;
    auto instance = fbl::AdoptRef<DummyIommu>(new (&ac) DummyIommu());
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    *out = fbl::move(instance);
    return ZX_OK;
}

DummyIommu::~DummyIommu() {
}

bool DummyIommu::IsValidBusTxnId(uint64_t bus_txn_id) const {
    return true;
}

zx_status_t DummyIommu::Map(uint64_t bus_txn_id, paddr_t paddr, size_t size, uint32_t perms,
                            dev_vaddr_t* vaddr) {
    DEBUG_ASSERT(vaddr);
    if (!IS_PAGE_ALIGNED(paddr) || !IS_PAGE_ALIGNED(size)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (perms & ~(IOMMU_FLAG_PERM_READ | IOMMU_FLAG_PERM_WRITE | IOMMU_FLAG_PERM_EXECUTE)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (perms == 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    *vaddr = paddr;
    return ZX_OK;
}

zx_status_t DummyIommu::Unmap(uint64_t bus_txn_id, dev_vaddr_t vaddr, size_t size) {
    if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(size)) {
        return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
}

zx_status_t DummyIommu::ClearMappingsForBusTxnId(uint64_t bus_txn_id) {
    return ZX_OK;
}
