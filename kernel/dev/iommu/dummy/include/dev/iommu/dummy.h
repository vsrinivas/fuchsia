// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/iommu.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/iommu.h>
#include <fbl/unique_ptr.h>

class DummyIommu final : public Iommu {
public:
    static zx_status_t Create(fbl::unique_ptr<const uint8_t[]> desc, uint32_t desc_len,
                              fbl::RefPtr<Iommu>* out);

    bool IsValidBusTxnId(uint64_t bus_txn_id) const final;

    zx_status_t Map(uint64_t bus_txn_id, paddr_t paddr, size_t size, uint32_t perms,
                    dev_vaddr_t* vaddr) final;
    zx_status_t Unmap(uint64_t bus_txn_id, dev_vaddr_t vaddr, size_t size) final;

    zx_status_t ClearMappingsForBusTxnId(uint64_t bus_txn_id) final;

    ~DummyIommu() final;

    DISALLOW_COPY_ASSIGN_AND_MOVE(DummyIommu);
private:
    DummyIommu();
};
