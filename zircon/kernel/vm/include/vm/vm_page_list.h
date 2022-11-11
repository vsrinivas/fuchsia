// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_VM_PAGE_LIST_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_VM_PAGE_LIST_H_

#include <align.h>
#include <bits.h>
#include <lib/fit/function.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <fbl/canary.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <ktl/algorithm.h>
#include <ktl/unique_ptr.h>
#include <vm/page.h>
#include <vm/pmm.h>
#include <vm/vm.h>

// RAII helper for representing content in a page list node. This supports being in one of three
// states
//  * Empty       - Contains nothing
//  * Page p      - Contains a vm_page 'p'. This 'p' is considered owned by this wrapper and
//                  `ReleasePage` must be called to give up ownership.
//  * Reference r - Contains a reference 'r' to some content. This 'r' is considered owned by this
//                  wrapper and `ReleaseReference` must be called to give up ownership.
//  * Marker      - Indicates that whilst not a page, it is also not empty. Markers can be used to
//                  separate the distinction between "there's no page because we've deduped to the
//                  zero page" and "there's no page because our parent contains the content".
class VmPageOrMarker {
 public:
  // A PageType that otherwise holds a null pointer is considered to be Empty.
  VmPageOrMarker() : raw_(kPageType) {}
  ~VmPageOrMarker() { DEBUG_ASSERT(!IsPageOrRef()); }
  VmPageOrMarker(VmPageOrMarker&& other) noexcept : raw_(other.Release()) {}
  VmPageOrMarker(const VmPageOrMarker&) = delete;
  VmPageOrMarker& operator=(const VmPageOrMarker&) = delete;

  // Minimal wrapper around a uint64_t to provide stronger typing in code to prevent accidental
  // mixing of references and other uint64_t values.
  // Provides a way to query the required alignment of the references and does debug enforcement of
  // this.
  class ReferenceValue {
   public:
    // kAlignBits represents the number of low bits in a reference that must be zero so they can be
    // used for internal metadata. This is declared here for convenience, and is asserted to be in
    // sync with the private kReferenceBits.
    static constexpr uint64_t kAlignBits = 4;
    explicit ReferenceValue(uint64_t raw) : value_(raw) {
      DEBUG_ASSERT((value_ & BIT_MASK(kAlignBits)) == 0);
    }
    uint64_t value() const { return value_; }

   private:
    uint64_t value_;
  };

  // Returns a reference to the underlying vm_page*. Is only valid to call if `IsPage` is true.
  vm_page* Page() const {
    DEBUG_ASSERT(IsPage());
    // Do not need to mask any bits out of raw_, since Page has 0's for the type anyway.
    static_assert(kPageType == 0);
    return reinterpret_cast<vm_page*>(raw_);
  }
  ReferenceValue Reference() const {
    DEBUG_ASSERT(IsReference());
    return ReferenceValue(raw_ & ~BIT_MASK(kReferenceBits));
  }

  // If this is a page, moves the underlying vm_page* out and returns it. After this IsPage will
  // be false and IsEmpty will be true.
  [[nodiscard]] vm_page* ReleasePage() {
    DEBUG_ASSERT(IsPage());
    // Do not need to mask any bits out of the Release since Page has 0's for the type
    // anyway.
    static_assert(kPageType == 0);
    return reinterpret_cast<vm_page*>(Release());
  }

  [[nodiscard]] ReferenceValue ReleaseReference() {
    DEBUG_ASSERT(IsReference());
    return ReferenceValue(Release() & ~BIT_MASK(kReferenceBits));
  }

  // Convenience wrappers for getting and setting split bits on both pages and references.
  bool PageOrRefLeftSplit() const {
    DEBUG_ASSERT(IsPageOrRef());
    if (IsPage()) {
      return Page()->object.cow_left_split;
    }
    return raw_ & kReferenceLeftSplit;
  }
  bool PageOrRefRightSplit() const {
    DEBUG_ASSERT(IsPageOrRef());
    if (IsPage()) {
      return Page()->object.cow_right_split;
    }
    return raw_ & kReferenceRightSplit;
  }
  void SetPageOrRefLeftSplit(bool value) {
    DEBUG_ASSERT(IsPageOrRef());
    if (IsPage()) {
      Page()->object.cow_left_split = value;
    } else {
      if (value) {
        raw_ |= kReferenceLeftSplit;
      } else {
        raw_ &= ~kReferenceLeftSplit;
      }
    }
  }
  void SetPageOrRefRightSplit(bool value) {
    DEBUG_ASSERT(IsPageOrRef());
    if (IsPage()) {
      Page()->object.cow_right_split = value;
    } else {
      if (value) {
        raw_ |= kReferenceRightSplit;
      } else {
        raw_ &= ~kReferenceRightSplit;
      }
    }
  }

  bool IsPage() const { return !IsEmpty() && (GetType() == kPageType); }
  bool IsMarker() const { return GetType() == kZeroMarkerType; }
  bool IsEmpty() const {
    // A PageType that otherwise holds a null pointer is considered to be Empty.
    return raw_ == kPageType;
  }
  bool IsReference() const { return GetType() == kReferenceType; }
  bool IsPageOrRef() const { return IsPage() || IsReference(); }

  VmPageOrMarker& operator=(VmPageOrMarker&& other) noexcept {
    // Forbid overriding content, as that would leak it.
    DEBUG_ASSERT(!IsPageOrRef());
    raw_ = other.Release();
    return *this;
  }

  bool operator==(const VmPageOrMarker& other) const { return raw_ == other.raw_; }

  bool operator!=(const VmPageOrMarker& other) const { return raw_ != other.raw_; }

  // A PageType that otherwise holds a null pointer is considered to be Empty.
  static VmPageOrMarker Empty() { return VmPageOrMarker{kPageType}; }
  static VmPageOrMarker Marker() { return VmPageOrMarker{kZeroMarkerType}; }

  [[nodiscard]] static VmPageOrMarker Page(vm_page* p) {
    // A null page is incorrect for two reasons
    // 1. It's a violation of the API of this method
    // 2. A null page cannot be represented internally as this is used to represent Empty
    DEBUG_ASSERT(p);
    const uint64_t raw = reinterpret_cast<uint64_t>(p);
    // A pointer should be aligned by definition, and hence the low bits should always be zero, but
    // assert this anyway just in case kTypeBits is increased or someone passed an invalid pointer.
    DEBUG_ASSERT((raw & BIT_MASK(kTypeBits)) == 0);
    return VmPageOrMarker{raw | kPageType};
  }

 private:
  explicit VmPageOrMarker(uint64_t raw) : raw_(raw) {}

  // The low 2 bits of raw_ are reserved to select the type, any other data has to fit into the
  // remaining high bits. Note that there is no explicit Empty type, rather a PageType with a zero
  // pointer is used to represent Empty.
  static constexpr uint64_t kTypeBits = 2;
  static constexpr uint64_t kPageType = 0b00;
  static constexpr uint64_t kZeroMarkerType = 0b01;
  static constexpr uint64_t kReferenceType = 0b10;

  // In addition to storing the type, a reference needs to track two additional pieces of data,
  // these being the left and right split bits. The split bits are normally stored in the vm_page_t
  // and are used for copy-on-write tracking in hidden VMOs. Having the ability to store the split
  // bits here allows these pages to be candidates for compression. The remaining bits are then
  // available for the actual reference value being stored. Unlike the page type, which does not
  // allow the 0 value to be stored, a reference has no restrictions and a ref value of 0 is valid
  // and may be stored.
  static constexpr uint64_t kReferenceBits = kTypeBits + 2;
  // Due to ordering and public/private visibility ReferenceValue::kAlignBits is declared
  // separately, but it should match kReferenceBits.
  static_assert(ReferenceValue::kAlignBits == kReferenceBits);
  static constexpr uint64_t kReferenceLeftSplit = 0b10 << kTypeBits;
  static constexpr uint64_t kReferenceRightSplit = 0b01 << kTypeBits;

  uint64_t GetType() const { return raw_ & BIT_MASK(kTypeBits); }

  // TODO(fxb/60238): Make this public once tests are added and all TODO code paths for handling
  // reference types are resolved.
  [[nodiscard]] static VmPageOrMarker Reference(ReferenceValue ref, bool left_split,
                                                bool right_split) {
    return VmPageOrMarker(ref.value() | (left_split ? kReferenceLeftSplit : 0) |
                          (right_split ? kReferenceRightSplit : 0) | kReferenceType);
  }

  uint64_t Release() {
    const uint64_t p = raw_;
    raw_ = 0;
    return p;
  }

  uint64_t raw_;
};

// Limited reference to a VmPageOrMarker. This reference provides unrestricted const access to the
// underlying VmPageOrMarker, but as it holds a non-const VmPageOrMarker* it has the ability to
// modify the underlying entry. However, the interface for modification is very limited.
//
// This allows for the majority of VmPageList iterations that are not intended to allow for clearing
// entries to the Empty state to allow limited mutation (such as between different content states),
// without being completely mutable.
class VmPageOrMarkerRef {
 public:
  VmPageOrMarkerRef() = default;
  explicit VmPageOrMarkerRef(VmPageOrMarker* page_or_marker) : page_or_marker_(page_or_marker) {}
  ~VmPageOrMarkerRef() = default;

  const VmPageOrMarker& operator*() const {
    DEBUG_ASSERT(page_or_marker_);
    return *page_or_marker_;
  }

  const VmPageOrMarker* operator->() const {
    DEBUG_ASSERT(page_or_marker_);
    return page_or_marker_;
  }

  explicit operator bool() const { return !!page_or_marker_; }

  // Forward split bit modifications as an allowed mutation.
  void SetPageOrRefLeftSplit(bool value) {
    DEBUG_ASSERT(page_or_marker_);
    page_or_marker_->SetPageOrRefLeftSplit(value);
  }
  void SetPageOrRefRightSplit(bool value) {
    DEBUG_ASSERT(page_or_marker_);
    page_or_marker_->SetPageOrRefRightSplit(value);
  }

  // TODO(fxb/60238): Add an equivalent SwapPageForReference method once tests are added and all
  // code paths for handling reference types are resolved.
  // Changing the kind of content is an allowed mutation and this takes ownership of the provided
  // page and returns ownership of the previous reference.
  [[nodiscard]] VmPageOrMarker::ReferenceValue SwapReferenceForPage(vm_page_t* p) {
    DEBUG_ASSERT(page_or_marker_);
    DEBUG_ASSERT(p);
    // Ensure the caller has correctly set the split bits in the page as this swap is not supposed
    // to change any other information.
    DEBUG_ASSERT(p->object.cow_left_split == page_or_marker_->PageOrRefLeftSplit());
    DEBUG_ASSERT(p->object.cow_right_split == page_or_marker_->PageOrRefRightSplit());
    VmPageOrMarker::ReferenceValue ref = page_or_marker_->ReleaseReference();
    *page_or_marker_ = VmPageOrMarker::Page(p);
    return ref;
  }

 private:
  VmPageOrMarker* page_or_marker_ = nullptr;
};

class VmPageListNode final : public fbl::WAVLTreeContainable<ktl::unique_ptr<VmPageListNode>> {
 public:
  explicit VmPageListNode(uint64_t offset);
  ~VmPageListNode();

  DISALLOW_COPY_ASSIGN_AND_MOVE(VmPageListNode);

  static const size_t kPageFanOut = 16;

  // accessors
  uint64_t offset() const { return obj_offset_; }
  uint64_t GetKey() const { return obj_offset_; }

  uint64_t end_offset() const { return offset() + kPageFanOut * PAGE_SIZE; }

  void set_offset(uint64_t offset) {
    DEBUG_ASSERT(!InContainer());
    obj_offset_ = offset;
  }

  // for every page or marker in the node call the passed in function.
  template <typename F>
  zx_status_t ForEveryPage(F func, uint64_t skew) {
    return ForEveryPageInRange(this, func, offset(), end_offset(), skew);
  }

  // for every page or marker in the node call the passed in function.
  template <typename F>
  zx_status_t ForEveryPage(F func, uint64_t skew) const {
    return ForEveryPageInRange(this, func, offset(), end_offset(), skew);
  }

  // for every page or marker in the node in the range call the passed in function. The range is
  // assumed to be within the nodes object range.
  template <typename F>
  zx_status_t ForEveryPageInRange(F func, uint64_t start_offset, uint64_t end_offset,
                                  uint64_t skew) {
    return ForEveryPageInRange(this, func, start_offset, end_offset, skew);
  }

  // for every page or marker in the node in the range call the passed in function. The range is
  // assumed to be within the nodes object range.
  template <typename F>
  zx_status_t ForEveryPageInRange(F func, uint64_t start_offset, uint64_t end_offset,
                                  uint64_t skew) const {
    return ForEveryPageInRange(this, func, start_offset, end_offset, skew);
  }

  const VmPageOrMarker& Lookup(size_t index) const {
    canary_.Assert();
    DEBUG_ASSERT(index < kPageFanOut);
    return pages_[index];
  }

  VmPageOrMarker& Lookup(size_t index) {
    canary_.Assert();
    DEBUG_ASSERT(index < kPageFanOut);
    return pages_[index];
  }

  // A node is empty if it contains no pages, references or markers.
  bool IsEmpty() const {
    for (const auto& p : pages_) {
      if (!p.IsEmpty()) {
        return false;
      }
    }
    return true;
  }

  // Returns true if there are still any pages or references owned by this node.
  bool HasNoPageOrRef() const {
    for (const auto& p : pages_) {
      if (p.IsPageOrRef()) {
        return false;
      }
    }
    return true;
  }

 private:
  template <typename S, typename F>
  static zx_status_t ForEveryPageInRange(S self, F func, uint64_t start_offset, uint64_t end_offset,
                                         uint64_t skew) {
    // Assert that the requested range is sensible and falls within our nodes actual offset range.
    DEBUG_ASSERT(end_offset >= start_offset);
    DEBUG_ASSERT(start_offset >= self->obj_offset_);
    DEBUG_ASSERT(end_offset <= self->end_offset());
    const size_t start = (start_offset - self->obj_offset_) / PAGE_SIZE;
    const size_t end = (end_offset - self->obj_offset_) / PAGE_SIZE;
    for (size_t i = start; i < end; i++) {
      if (!self->pages_[i].IsEmpty()) {
        zx_status_t status = func(&self->pages_[i], self->obj_offset_ + i * PAGE_SIZE - skew);
        if (unlikely(status != ZX_ERR_NEXT)) {
          return status;
        }
      }
    }
    return ZX_ERR_NEXT;
  }

  fbl::Canary<fbl::magic("PLST")> canary_;

  uint64_t obj_offset_ = 0;
  VmPageOrMarker pages_[kPageFanOut];
};

class VmPageList;

// Class which holds the list of vm_page structs removed from a VmPageList
// by TakePages. The list include information about uncommitted pages and markers.
class VmPageSpliceList final {
 public:
  VmPageSpliceList();
  VmPageSpliceList(VmPageSpliceList&& other);
  VmPageSpliceList& operator=(VmPageSpliceList&& other_tree);
  ~VmPageSpliceList();

  // For use by PhysicalPageProvider.  The user-pager path doesn't use this.
  static VmPageSpliceList CreateFromPageList(uint64_t offset, uint64_t length, list_node* pages);

  // Pops the next page off of the splice.
  VmPageOrMarker Pop();

  // Returns true after the whole collection has been processed by Pop.
  bool IsDone() const { return pos_ >= length_; }

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(VmPageSpliceList);

 private:
  VmPageSpliceList(uint64_t offset, uint64_t length);
  void FreeAllPages();

  uint64_t offset_;
  uint64_t length_;
  uint64_t pos_ = 0;

  VmPageListNode head_ = VmPageListNode(0);
  fbl::WAVLTree<uint64_t, ktl::unique_ptr<VmPageListNode>> middle_;
  VmPageListNode tail_ = VmPageListNode(0);

  // To avoid the possibility of allocation failure, we don't use head_, middle_, tail_ for
  // CreateFromPageList().  With CreateFromPageList() we know that all the pages are present, so
  // we can just keep a list of pages, and create VmPageListNode on the stack as pages are Pop()ed.
  list_node raw_pages_ = LIST_INITIAL_VALUE(raw_pages_);

  friend VmPageList;
};

class VmPageList final {
 public:
  VmPageList();
  ~VmPageList();

  VmPageList& operator=(VmPageList&& other);
  VmPageList(VmPageList&& other);

  void InitializeSkew(uint64_t parent_skew, uint64_t offset) {
    // Checking list_skew_ doesn't catch all instances of double-initialization, but
    // it should catch some of them.
    DEBUG_ASSERT(list_skew_ == 0);
    DEBUG_ASSERT(list_.is_empty());

    list_skew_ = (parent_skew + offset) % (PAGE_SIZE * VmPageListNode::kPageFanOut);
  }
  uint64_t GetSkew() const { return list_skew_; }

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(VmPageList);

  // walk the page tree, calling the passed in function on every tree node.
  template <typename F>
  zx_status_t ForEveryPage(F per_page_func) const {
    return ForEveryPage(this, per_page_func);
  }

  // walk the page tree, calling the passed in function on every tree node.
  template <typename F>
  zx_status_t ForEveryPageInRange(F per_page_func, uint64_t start_offset,
                                  uint64_t end_offset) const {
    return ForEveryPageInRange(this, per_page_func, start_offset, end_offset);
  }

  // walk the page tree, calling |per_page_func| on every page/marker and |per_gap_func| on every
  // gap.
  template <typename PAGE_FUNC, typename GAP_FUNC>
  zx_status_t ForEveryPageAndGapInRange(PAGE_FUNC per_page_func, GAP_FUNC per_gap_func,
                                        uint64_t start_offset, uint64_t end_offset) const {
    return ForEveryPageAndGapInRange(this, per_page_func, per_gap_func, start_offset, end_offset);
  }

  // walk the page tree, calling |per_page_func| on every page/marker that fulfills (returns true)
  // the |compare_func|. Also call |contiguous_run_func| on every contiguous range of such
  // pages/markers encountered.
  template <typename COMPARE_FUNC, typename PAGE_FUNC, typename CONTIGUOUS_RUN_FUNC>
  zx_status_t ForEveryPageAndContiguousRunInRange(COMPARE_FUNC compare_func,
                                                  PAGE_FUNC per_page_func,
                                                  CONTIGUOUS_RUN_FUNC contiguous_run_func,
                                                  uint64_t start_offset,
                                                  uint64_t end_offset) const {
    return ForEveryPageAndContiguousRunInRange(this, compare_func, per_page_func,
                                               contiguous_run_func, start_offset, end_offset);
  }

  // Returns true if any pages or markers are in the given range.
  bool AnyPagesInRange(uint64_t start_offset, uint64_t end_offset) const {
    bool found_page = false;
    ForEveryPageInRange(
        [&found_page](const VmPageOrMarker* page, uint64_t offset) {
          found_page = true;
          return ZX_ERR_STOP;
        },
        start_offset, end_offset);
    return found_page;
  }

  // Attempts to return a reference to the VmPageOrMarker at the specified offset. The returned
  // pointer is valid until the VmPageList is destroyed or any of the Remove*/Take/Merge etc
  // functions are called.
  //
  // Lookup may return 'nullptr' if there is no slot allocated for the given offset. If non-null
  // is returned it may still be the case that IsEmpty() on the returned PageOrMarker is true.
  const VmPageOrMarker* Lookup(uint64_t offset) const;

  // Similar to `Lookup` but returns a VmPageOrMarkerRef that allows for limited mutation of the
  // slot. General mutation requires calling `LookupOrAllocate`.
  VmPageOrMarkerRef LookupMutable(uint64_t offset);

  // Similar to `Lookup` but only returns `nullptr` if a slot cannot be allocated either due to out
  // of memory or due to offset being invalid.
  //
  // The returned slot, if not a `nullptr`, may generally be freely manipulated with the exception
  // that if it started !Empty, then it is an error to set it to Empty. In this case the
  // `RemovePage` method must be used.
  //
  // If the returned slot started Empty, as it not made !Empty, then the slot must be returned with
  // ReturnEmptySlot, to ensure no empty nodes are retained.
  VmPageOrMarker* LookupOrAllocate(uint64_t offset);

  // Returns a slot that was empty after LookupOrAllocate, and that the caller did not end up
  // filling.
  // This ensures that if LookupOrAllocate allocated a new underlying list node, then that list node
  // needs to be free'd otherwise it might not get cleaned up for the lifetime of the page list.
  //
  // This is only correct to call on an offset for which LookupOrAllocate had just returned a non
  // null slot, and that slot was Empty and is still Empty.
  void ReturnEmptySlot(uint64_t offset);

  // Removes any item at |offset| from the list and returns it, or VmPageOrMarker::Empty() if none.
  VmPageOrMarker RemoveContent(uint64_t offset);

  // Release every item in the page list and calls free_content_fn on any content, giving it
  // ownership. Any markers are cleared.
  template <typename T>
  void RemoveAllContent(T free_content_fn) {
    // per page get a reference to the page pointer inside the page list node
    auto per_page_func = [&free_content_fn](VmPageOrMarker* p, uint64_t offset) {
      if (p->IsPageOrRef()) {
        free_content_fn(ktl::move(*p));
      }
      *p = VmPageOrMarker::Empty();
      return ZX_ERR_NEXT;
    };

    // walk the tree in order, freeing all the pages on every node
    ForEveryPage(this, per_page_func);

    // empty the tree
    list_.clear();
  }

  // Calls the provided callback for every page or marker in the range [start_offset, end_offset).
  // The callback can modify the VmPageOrMarker and take ownership of any pages, or leave them in
  // place. The difference between this and ForEveryPage is as this allows for modifying the
  // underlying pages any intermediate data structures can be checked and potentially freed if no
  // longer needed.
  template <typename T>
  void RemovePages(T per_page_fn, uint64_t start_offset, uint64_t end_offset) {
    ForEveryPageInRange<NodeCheck::CleanupEmpty>(this, per_page_fn, start_offset, end_offset);
  }

  // Similar to RemovePages but also takes a |per_gap_fn| callback to allow for iterating over any
  // gaps encountered as well. This can be used when the intent is to modify the underlying pages
  // and/or gaps, while checking any intermediate data structures to potentially free ones that are
  // no longer needed.
  template <typename P, typename G>
  zx_status_t RemovePagesAndIterateGaps(P per_page_fn, G per_gap_fn, uint64_t start_offset,
                                        uint64_t end_offset) {
    return ForEveryPageAndGapInRange<NodeCheck::CleanupEmpty>(this, per_page_fn, per_gap_fn,
                                                              start_offset, end_offset);
  }

  // Returns true if there are no pages, references or markers in the page list.
  bool IsEmpty() const;

  // Returns true if the page list does not own any pages or references.
  bool HasNoPageOrRef() const;

  // Merges the pages in |other| in the range [|offset|, |end_offset|) into |this|
  // page list, starting at offset 0 in this list.
  //
  // For every page in |other| in the given range, if there is no corresponding page or marker
  // in |this|, then they will be passed to |migrate_fn|. If |migrate_fn| leaves the page in the
  // VmPageOrMarker it will be migrated into |this|, otherwise the migrate_fn is assumed to now own
  // the page. For any pages or markers in |other| outside the given range or which conflict with a
  // page in |this|, they will be released given ownership to |release_fn|.
  //
  // The |offset| values passed to |release_fn| and |migrate_fn| are the original offsets
  // in |other|, not the adapted offsets in |this|.
  //
  // **NOTE** unlike MergeOnto, |other| will be empty at the end of this method.
  void MergeFrom(
      VmPageList& other, uint64_t offset, uint64_t end_offset,
      fit::inline_function<void(VmPageOrMarker&&, uint64_t offset), 3 * sizeof(void*)> release_fn,
      fit::inline_function<void(VmPageOrMarker*, uint64_t offset)> migrate_fn);

  // Merges this pages in |this| onto |other|.
  //
  // For every page (or marker) in |this|, checks the same offset in |other|. If there is no
  // page or marker, then it inserts the page into |other|. Otherwise, it releases the page (or
  // marker) and gives ownership to |release_fn|.
  //
  // **NOTE** unlike MergeFrom, |this| will be empty at the end of this method.
  void MergeOnto(VmPageList& other, fit::inline_function<void(VmPageOrMarker&&)> release_fn);

  // Takes the pages, references and markers in the range [offset, length) out of this page list.
  VmPageSpliceList TakePages(uint64_t offset, uint64_t length);

  uint64_t HeapAllocationBytes() const { return list_.size() * sizeof(VmPageListNode); }

  // Allow the implementation to use a one-past-the-end for VmPageListNode offsets,
  // plus to account for skew_.
  static constexpr uint64_t MAX_SIZE =
      ROUNDDOWN(UINT64_MAX, 2 * VmPageListNode::kPageFanOut * PAGE_SIZE);

 private:
  template <typename S, typename F>
  static zx_status_t ForEveryPage(S self, F per_page_func) {
    for (auto& pl : self->list_) {
      zx_status_t status = pl.ForEveryPage(per_page_func, self->list_skew_);
      if (unlikely(status != ZX_ERR_NEXT)) {
        if (status == ZX_ERR_STOP) {
          break;
        }
        return status;
      }
    }
    return ZX_OK;
  }

  // Calls the provided callback for every page in the given range. If the CleanupNodes template
  // argument is true then it is assumed the per_page_func may remove pages and page nodes will be
  // checked to see if they are empty and can be cleaned up.
  enum class NodeCheck : bool {
    Skip = false,
    CleanupEmpty = true,
  };
  template <NodeCheck NODE_CHECK = NodeCheck::Skip, typename S, typename F>
  static zx_status_t ForEveryPageInRange(S self, F per_page_func, uint64_t start_offset,
                                         uint64_t end_offset) {
    start_offset += self->list_skew_;
    end_offset += self->list_skew_;

    // Find the first node (if any) that will contain our starting offset.
    auto cur =
        self->list_.lower_bound(ROUNDDOWN(start_offset, VmPageListNode::kPageFanOut * PAGE_SIZE));
    if (!cur) {
      return ZX_OK;
    }

    // Handle scenario where start_offset begins not aligned to a node.
    if (cur->offset() < start_offset) {
      zx_status_t status = cur->ForEveryPageInRange(
          per_page_func, start_offset, ktl::min(end_offset, cur->end_offset()), self->list_skew_);
      auto prev = cur++;
      if constexpr (NODE_CHECK == NodeCheck::CleanupEmpty) {
        if (prev->IsEmpty()) {
          self->list_.erase(prev);
        }
      }
      if (unlikely(status != ZX_ERR_NEXT)) {
        if (status == ZX_ERR_STOP) {
          return ZX_OK;
        }
        return status;
      }
    }
    // Iterate through all full nodes contained in the range.
    while (cur && cur->end_offset() < end_offset) {
      DEBUG_ASSERT(start_offset <= cur->offset());
      zx_status_t status = cur->ForEveryPage(per_page_func, self->list_skew_);
      auto prev = cur++;
      if constexpr (NODE_CHECK == NodeCheck::CleanupEmpty) {
        if (prev->IsEmpty()) {
          self->list_.erase(prev);
        }
      }
      if (unlikely(status != ZX_ERR_NEXT)) {
        if (status == ZX_ERR_STOP) {
          return ZX_OK;
        }
        return status;
      }
    }
    // Handle scenario where the end_offset is not aligned to the end of a node.
    if (cur && cur->offset() < end_offset) {
      DEBUG_ASSERT(cur->end_offset() >= end_offset);
      zx_status_t status =
          cur->ForEveryPageInRange(per_page_func, cur->offset(), end_offset, self->list_skew_);
      if constexpr (NODE_CHECK == NodeCheck::CleanupEmpty) {
        if (cur->IsEmpty()) {
          self->list_.erase(cur);
        }
      }
      if (unlikely(status != ZX_ERR_NEXT)) {
        if (status == ZX_ERR_STOP) {
          return ZX_OK;
        }
        return status;
      }
    }
    return ZX_OK;
  }

  template <NodeCheck NODE_CHECK = NodeCheck::Skip, typename S, typename PAGE_FUNC,
            typename GAP_FUNC>
  static zx_status_t ForEveryPageAndGapInRange(S self, PAGE_FUNC per_page_func,
                                               GAP_FUNC per_gap_func, uint64_t start_offset,
                                               uint64_t end_offset) {
    uint64_t expected_next_off = start_offset;
    auto per_page_wrapper_fn = [&expected_next_off, end_offset, per_page_func, &per_gap_func](
                                   auto* p, uint64_t off) {
      zx_status_t status = ZX_ERR_NEXT;
      if (expected_next_off != off) {
        status = per_gap_func(expected_next_off, off);
      }
      if (status == ZX_ERR_NEXT) {
        status = per_page_func(p, off);
      }
      expected_next_off = off + PAGE_SIZE;
      // Prevent the last call to per_gap_func
      if (status == ZX_ERR_STOP) {
        expected_next_off = end_offset;
      }
      return status;
    };

    zx_status_t status =
        ForEveryPageInRange<NODE_CHECK>(self, per_page_wrapper_fn, start_offset, end_offset);
    if (status != ZX_OK) {
      return status;
    }

    if (expected_next_off != end_offset) {
      status = per_gap_func(expected_next_off, end_offset);
      if (status != ZX_ERR_NEXT && status != ZX_ERR_STOP) {
        return status;
      }
    }

    return ZX_OK;
  }

  template <typename S, typename COMPARE_FUNC, typename PAGE_FUNC, typename CONTIGUOUS_RUN_FUNC>
  static zx_status_t ForEveryPageAndContiguousRunInRange(S self, COMPARE_FUNC compare_func,
                                                         PAGE_FUNC per_page_func,
                                                         CONTIGUOUS_RUN_FUNC contiguous_run_func,
                                                         uint64_t start_offset,
                                                         uint64_t end_offset) {
    // Track contiguous range of pages fulfilling compare_func.
    uint64_t contiguous_run_start = start_offset;
    uint64_t contiguous_run_len = 0;

    zx_status_t status = ForEveryPageAndGapInRange(
        self,
        [&](const VmPageOrMarker* p, uint64_t off) {
          zx_status_t st = ZX_ERR_NEXT;
          if (compare_func(p, off)) {
            st = per_page_func(p, off);
            if (st == ZX_ERR_STOP) {
              return ZX_OK;
            }
            if (st != ZX_ERR_NEXT) {
              return st;
            }
            // Start tracking a new range first if no range is being tracked yet.
            if (contiguous_run_len == 0) {
              contiguous_run_start = off;
            }
            // Append this page to the contiguous range being tracked.
            contiguous_run_len += PAGE_SIZE;
            return ZX_ERR_NEXT;
          }
          // We were already tracking a contiguous range when we encountered this page that does not
          // fulfill compare_func. Invoke contiguous_run_func on the range so far and start tracking
          // a new one skipping over this page.
          if (contiguous_run_len > 0) {
            st = contiguous_run_func(contiguous_run_start,
                                     contiguous_run_start + contiguous_run_len);
            if (st == ZX_ERR_STOP) {
              return ZX_OK;
            }
            if (st != ZX_ERR_NEXT) {
              return st;
            }
          }
          // Reset contiguous_run_len to zero to track a new range later if required.
          contiguous_run_len = 0;
          return ZX_ERR_NEXT;
        },
        [&](uint64_t start, uint64_t end) {
          // We were already tracking a contiguous range when we encountered this gap. Invoke
          // contiguous_run_func on the range so far and start tracking a new one skipping over this
          // gap.
          if (contiguous_run_len > 0) {
            zx_status_t st = contiguous_run_func(contiguous_run_start,
                                                 contiguous_run_start + contiguous_run_len);
            if (st == ZX_ERR_STOP) {
              return ZX_OK;
            }
            if (st != ZX_ERR_NEXT) {
              return st;
            }
          }
          // Reset contiguous_run_len to zero to track a new range later if required.
          contiguous_run_len = 0;
          return ZX_ERR_NEXT;
        },
        start_offset, end_offset);

    if (status != ZX_OK) {
      return status;
    }

    // Process the last contiguous range if there is one.
    if (contiguous_run_len > 0) {
      status = contiguous_run_func(contiguous_run_start, contiguous_run_start + contiguous_run_len);
      if (status != ZX_ERR_NEXT && status != ZX_ERR_STOP) {
        return status;
      }
    }

    return ZX_OK;
  }

  fbl::WAVLTree<uint64_t, ktl::unique_ptr<VmPageListNode>> list_;
  // A skew added to offsets provided as arguments to VmPageList functions before
  // interfacing with list_. This allows all VmPageLists within a clone tree
  // to place individual vm_page_t entries at the same offsets within their nodes, so
  // that the nodes can be moved between different lists without having to worry
  // about needing to split up a node.
  uint64_t list_skew_ = 0;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_VM_PAGE_LIST_H_
