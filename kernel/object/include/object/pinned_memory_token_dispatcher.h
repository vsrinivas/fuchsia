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
#include <fbl/ref_ptr.h>
#include <object/dispatcher.h>
#include <zircon/rights.h>

#include <sys/types.h>

class BusTransactionInitiatorDispatcher;
class VmObject;

class PinnedMemoryTokenDispatcher final : public SoloDispatcher,
    public fbl::DoublyLinkedListable<PinnedMemoryTokenDispatcher*> {
public:
    ~PinnedMemoryTokenDispatcher();

    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_PMT; }
    bool has_state_tracker() const final { return false; }
    void on_zero_handles() final;

    // Traits to belong in the BTI's list.
    struct PinnedMemoryTokenListTraits {
        static fbl::DoublyLinkedListNodeState<PinnedMemoryTokenDispatcher*>& node_state(
            PinnedMemoryTokenDispatcher& obj) {
            return obj.dll_pmt_;
        }
    };
    // Traits to belong in the BTI's legacy list.
    // TODO(teisenbe): Remove this once bti_unpin syscall is gone
    struct LegacyPinnedMemoryTokenListTraits {
        static fbl::DoublyLinkedListNodeState<fbl::RefPtr<PinnedMemoryTokenDispatcher>>& node_state(
            PinnedMemoryTokenDispatcher& obj) {
            return obj.dll_pmt_legacy_;
        }
    };

    // Mark this PMT as unpinned.  When on_zero_handles() runs, this PMT will
    // be removed from its BTI rather than moved to the quarantine.
    void MarkUnpinned();

    // |mapped_addrs_count| must be either
    // 1) If |compress_results|, |size_|/|bti_.minimum_contiguity()|, rounded up, in which
    // case each returned address represents a run of |bti_.minimum_contiguity()| bytes (with
    // the exception of the last which may be short)
    // 2) Otherwise, |size_|/|PAGE_SIZE|, in which case each returned address represents a
    // single page.
    //
    // Returns ZX_ERR_INVALID_ARGS if |mapped_addrs_count| is not exactly the value described above.
    zx_status_t EncodeAddrs(bool compress_results, dev_vaddr_t* mapped_addrs, size_t mapped_addrs_count);
protected:
    friend BusTransactionInitiatorDispatcher;
    // Pin memory in |vmo|'s range [offset, offset+size) on behalf of |bti|,
    // with permissions specified by |perms|.  |perms| should be flags suitable
    // for the Iommu::Map() interface.  Must be created under the BTI
    // dispatcher's lock.
    static zx_status_t Create(fbl::RefPtr<BusTransactionInitiatorDispatcher> bti,
                              fbl::RefPtr<VmObject> vmo, size_t offset,
                              size_t size, uint32_t perms,
                              fbl::RefPtr<Dispatcher>* dispatcher,
                              zx_rights_t* rights);

    // Returns an array of the addrs usable by the given device
    // TODO(teisenbe): Remove this once zx_bti_unpin is gone
    const fbl::Array<dev_vaddr_t>& mapped_addrs() const { return mapped_addrs_; }
private:
    PinnedMemoryTokenDispatcher(fbl::RefPtr<BusTransactionInitiatorDispatcher> bti,
                                 fbl::RefPtr<VmObject> vmo, size_t offset, size_t size,
                                 bool is_contiguous,
                                 fbl::Array<dev_vaddr_t> mapped_addrs);
    DISALLOW_COPY_ASSIGN_AND_MOVE(PinnedMemoryTokenDispatcher);

    zx_status_t MapIntoIommu(uint32_t perms);
    zx_status_t UnmapFromIommuLocked() TA_REQ(get_lock());

    void InvalidateMappedAddrsLocked() TA_REQ(get_lock());

    fbl::Canary<fbl::magic("PMT_")> canary_;
    // The containing BTI holds a list of all its PMTs.
    fbl::DoublyLinkedListNodeState<PinnedMemoryTokenDispatcher*> dll_pmt_;
    // TODO(teisenbe): Remove this once bti_unpin syscall is gone
    fbl::DoublyLinkedListNodeState<fbl::RefPtr<PinnedMemoryTokenDispatcher>> dll_pmt_legacy_;

    const fbl::RefPtr<VmObject> vmo_;
    const uint64_t offset_;
    const uint64_t size_;
    const bool is_contiguous_;

    // Set to true by MarkUnpinned()
    bool explicitly_unpinned_ TA_GUARDED(get_lock()) = false;

    const fbl::RefPtr<BusTransactionInitiatorDispatcher> bti_;
    const fbl::Array<dev_vaddr_t> mapped_addrs_ TA_GUARDED(get_lock());
};
