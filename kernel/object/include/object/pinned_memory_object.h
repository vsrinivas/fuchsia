// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/iommu.h>
#include <err.h>
#include <fbl/array.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/unique_ptr.h>

#include <sys/types.h>

class BusTransactionInitiatorDispatcher;
class VmObject;

class PinnedMemoryObject final : public fbl::DoublyLinkedListable<fbl::unique_ptr<PinnedMemoryObject>> {
public:
    // Pin memory in |vmo|'s range [offset, offset+size) on behalf of |bti|,
    // with permissions specified by |perms|.  |perms| should be flags suitable
    // for the Iommu::Map() interface.
    static zx_status_t Create(const BusTransactionInitiatorDispatcher& bti,
                              fbl::RefPtr<VmObject> vmo, size_t offset,
                              size_t size, uint32_t perms,
                              fbl::unique_ptr<PinnedMemoryObject>* out);
    ~PinnedMemoryObject();

    // Returns an array of the addrs usable by the given device
    const fbl::Array<dev_vaddr_t>& mapped_addrs() const { return mapped_addrs_; }
private:
    PinnedMemoryObject(const BusTransactionInitiatorDispatcher& bti,
                       fbl::RefPtr<VmObject> vmo, size_t offset, size_t size,
                       bool is_contiguous,
                       fbl::Array<dev_vaddr_t> mapped_addrs);
    DISALLOW_COPY_ASSIGN_AND_MOVE(PinnedMemoryObject);

    zx_status_t MapIntoIommu(uint32_t perms);
    zx_status_t UnmapFromIommu();

    void InvalidateMappedAddrs();

    fbl::Canary<fbl::magic("PMO_")> canary_;

    const fbl::RefPtr<VmObject> vmo_;
    const uint64_t offset_;
    const uint64_t size_;
    const bool is_contiguous_;

    const BusTransactionInitiatorDispatcher& bti_;
    const fbl::Array<dev_vaddr_t> mapped_addrs_;
};
