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

zx_status_t DummyIommu::Map(uint64_t bus_txn_id, const fbl::RefPtr<VmObject>& vmo,
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
    if (offset + size < offset || offset + size > vmo->size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    auto lookup_fn = [](void* ctx, size_t offset, size_t index, paddr_t pa) {
        paddr_t* paddr = static_cast<paddr_t*>(ctx);
        *paddr = pa;
        return ZX_OK;
    };

    paddr_t paddr = UINT64_MAX;
    zx_status_t status = vmo->Lookup(offset, fbl::min<size_t>(PAGE_SIZE, size), 0, lookup_fn,
                                     &paddr);
    if (status != ZX_OK) {
        return status;
    }
    if (paddr == UINT64_MAX) {
        return ZX_ERR_BAD_STATE;
    }

    if (vmo->is_paged()) {
        *vaddr = paddr;
        *mapped_len = PAGE_SIZE;
    } else {
        *vaddr = paddr;
        *mapped_len = ROUNDUP(size, PAGE_SIZE);
    }
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

uint64_t DummyIommu::minimum_contiguity(uint64_t bus_txn_id) const {
    return PAGE_SIZE;
}

uint64_t DummyIommu::aspace_size(uint64_t bus_txn_id) const {
    return UINT64_MAX;
}
