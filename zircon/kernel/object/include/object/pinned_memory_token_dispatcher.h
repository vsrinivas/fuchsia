// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PINNED_MEMORY_TOKEN_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PINNED_MEMORY_TOKEN_DISPATCHER_H_

#include <err.h>
#include <sys/types.h>
#include <zircon/rights.h>

#include <dev/iommu.h>
#include <fbl/array.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <object/dispatcher.h>
#include <object/handle.h>
#include <vm/pinned_vm_object.h>

class BusTransactionInitiatorDispatcher;
class VmObject;

class PinnedMemoryTokenDispatcher final
    : public SoloDispatcher<PinnedMemoryTokenDispatcher, ZX_DEFAULT_PMT_RIGHTS>,
      public fbl::DoublyLinkedListable<PinnedMemoryTokenDispatcher*> {
 public:
  ~PinnedMemoryTokenDispatcher();

  zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_PMT; }
  void on_zero_handles() final;

  // Traits to belong in the BTI's list.
  struct PinnedMemoryTokenListTraits {
    static fbl::DoublyLinkedListNodeState<PinnedMemoryTokenDispatcher*>& node_state(
        PinnedMemoryTokenDispatcher& obj) {
      return obj.dll_pmt_;
    }
  };

  using QuarantineListNodeState =
      fbl::DoublyLinkedListNodeState<fbl::RefPtr<PinnedMemoryTokenDispatcher>>;
  struct QuarantineListTraits {
    static QuarantineListNodeState& node_state(PinnedMemoryTokenDispatcher& obj) {
      return obj.dll_quarantine_;
    }
  };

  // Unpin this PMT. If this is not done before on_zero_handles() runs, then it will get moved to
  // the quarantine.
  void Unpin();

  // |mapped_addrs_count| must be either
  // 1) If |compress_results|, |pinned_vmo_.size()|/|bti_.minimum_contiguity()|, rounded up, in
  // which case each returned address represents a run of |bti_.minimum_contiguity()| bytes (with
  // the exception of the last which may be short)
  // 2) If |contiguous|, 1, in which case the returned address is the start of the
  // contiguous memory.
  // 3) Otherwise, |pinned_vmo_.size()|/|PAGE_SIZE|, in which case each returned address
  // represents a single page.
  //
  // Returns ZX_ERR_INVALID_ARGS if |mapped_addrs_count| is not exactly the value described above.
  zx_status_t EncodeAddrs(bool compress_results, bool contiguous, dev_vaddr_t* mapped_addrs,
                          size_t mapped_addrs_count);

  // Returns the number of bytes pinned by the PMT.
  uint64_t size() const { return pinned_vmo_.size(); }

 protected:
  friend BusTransactionInitiatorDispatcher;
  // Set the permissions of |pinned_vmo|'s pinned range to |perms| on
  // behalf of |bti|. |perms| should be flags suitable for the Iommu::Map()
  // interface.  Must be created under the BTI dispatcher's lock.
  static zx_status_t Create(fbl::RefPtr<BusTransactionInitiatorDispatcher> bti,
                            PinnedVmObject pinned_vmo, uint32_t perms,
                            KernelHandle<PinnedMemoryTokenDispatcher>* handle, zx_rights_t* rights);

 private:
  PinnedMemoryTokenDispatcher(fbl::RefPtr<BusTransactionInitiatorDispatcher> bti,
                              PinnedVmObject pinned_vmo, fbl::Array<dev_vaddr_t> mapped_addrs);
  DISALLOW_COPY_ASSIGN_AND_MOVE(PinnedMemoryTokenDispatcher);

  zx_status_t MapIntoIommu(uint32_t perms);
  zx_status_t UnmapFromIommuLocked() TA_REQ(get_lock());

  void InvalidateMappedAddrsLocked() TA_REQ(get_lock());

  // The containing BTI holds a list of all its PMTs, including those which are quarantined.
  fbl::DoublyLinkedListNodeState<PinnedMemoryTokenDispatcher*> dll_pmt_;
  // The containing BTI holds a list of all its quarantined PMTs.
  QuarantineListNodeState dll_quarantine_;

  PinnedVmObject pinned_vmo_;

  // Set to true by Unpin()
  bool explicitly_unpinned_ TA_GUARDED(get_lock()) = false;

  const fbl::RefPtr<BusTransactionInitiatorDispatcher> bti_;
  const fbl::Array<dev_vaddr_t> mapped_addrs_ TA_GUARDED(get_lock());

  // Set to true during Create() once we are fully initialized. Do not call
  // any |bti_| locking methods if this is false, since that indicates we're
  // being called from Create() and already have the |bti_| lock.
  bool initialized_ = false;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_PINNED_MEMORY_TOKEN_DISPATCHER_H_
