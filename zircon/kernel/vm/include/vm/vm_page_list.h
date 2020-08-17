// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_VM_PAGE_LIST_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_VM_PAGE_LIST_H_

#include <align.h>
#include <err.h>
#include <zircon/types.h>

#include <fbl/canary.h>
#include <fbl/function.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <ktl/unique_ptr.h>
#include <vm/page.h>
#include <vm/pmm.h>
#include <vm/vm.h>

// RAII helper for representing owned pages in a page list node. This supports being in one of
// three states
//  * Empty - Contains nothing
//  * Page p - Contains a vm_page 'p'. This 'p' is considered owned by this wrapper and
//             `ReleasePage` must be called to give up ownership.
//  * Marker - Indicates that whilst not a page, it is also not empty. Markers can be used to
//             separate the distinction between "there's no page because we've deduped to the zero
//             page" and "there's no page because our parent contains the content".
class VmPageOrMarker {
 public:
  VmPageOrMarker() : page_(nullptr) {}
  ~VmPageOrMarker() { DEBUG_ASSERT(!IsPage()); }
  VmPageOrMarker(VmPageOrMarker&& other) : page_(other.Release()) {}
  VmPageOrMarker(const VmPageOrMarker&) = delete;
  VmPageOrMarker& operator=(const VmPageOrMarker&) = delete;

  // Returns a reference to the underlying vm_page*. Is only valid to call if `IsPage` is true.
  vm_page* Page() const {
    DEBUG_ASSERT(IsPage());
    return page_;
  }

  // If this is a page, moves the underlying vm_page* out and returns it. After this IsPage will
  // be false and IsEmpty will be true.
  vm_page* ReleasePage() {
    DEBUG_ASSERT(IsPage());
    return Release();
  }

  bool IsPage() const { return !IsMarker() && !IsEmpty(); }

  bool IsMarker() const { return page_ == RawMarker(); }

  bool IsEmpty() const { return page_ == nullptr; }

  VmPageOrMarker& operator=(VmPageOrMarker&& other) {
    // Forbid overriding a page, as that would leak it.
    DEBUG_ASSERT(!IsPage());
    page_ = other.Release();
    return *this;
  }

  bool operator==(const VmPageOrMarker& other) const { return page_ == other.page_; }

  bool operator!=(const VmPageOrMarker& other) const { return page_ != other.page_; }

  static VmPageOrMarker Empty() { return {nullptr}; }

  static VmPageOrMarker Marker() { return {RawMarker()}; }

  static VmPageOrMarker Page(vm_page* p) {
    DEBUG_ASSERT(p);
    return {p};
  }

 private:
  VmPageOrMarker(vm_page* p) : page_(p) {}

  static vm_page* RawMarker() { return reinterpret_cast<vm_page*>(1); }

  vm_page* Release() {
    vm_page* p = page_;
    page_ = nullptr;
    return p;
  }

  vm_page* page_;
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

  void set_offset(uint64_t offset) {
    DEBUG_ASSERT(!InContainer());
    obj_offset_ = offset;
  }

  // for every page or marker in the node call the passed in function.
  template <typename F>
  zx_status_t ForEveryPage(F func, uint64_t start_offset, uint64_t end_offset, uint64_t skew) {
    return ForEveryPage(this, func, start_offset, end_offset, skew);
  }

  // for every page or marker in the node call the passed in function.
  template <typename F>
  zx_status_t ForEveryPage(F func, uint64_t start_offset, uint64_t end_offset,
                           uint64_t skew) const {
    return ForEveryPage(this, func, start_offset, end_offset, skew);
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

  // A node is empty if it contains no pages or markers.
  bool IsEmpty() const {
    for (const auto& p : pages_) {
      if (!p.IsEmpty()) {
        return false;
      }
    }
    return true;
  }

  // Returns true if there are still allocated vm_page_t's owned by this node.
  bool HasNoPages() const {
    for (const auto& p : pages_) {
      if (p.IsPage()) {
        return false;
      }
    }
    return true;
  }

 private:
  template <typename S, typename F>
  static zx_status_t ForEveryPage(S self, F func, uint64_t start_offset, uint64_t end_offset,
                                  uint64_t skew) {
    DEBUG_ASSERT(end_offset >= start_offset);
    size_t start = 0;
    size_t end = kPageFanOut;
    if (start_offset > self->obj_offset_) {
      start = (start_offset - self->obj_offset_) / PAGE_SIZE;
    }
    if (end_offset < self->obj_offset_) {
      return ZX_ERR_NEXT;
    }
    if (end_offset < self->obj_offset_ + kPageFanOut * PAGE_SIZE) {
      end = (end_offset - self->obj_offset_) / PAGE_SIZE;
    }
    for (size_t i = start; i < end; i++) {
      if (!self->pages_[i].IsEmpty()) {
        zx_status_t status = func(self->pages_[i], self->obj_offset_ + i * PAGE_SIZE - skew);
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

  // Attempts to return a reference to the VmPageOrMarker at the specified offset. The returned
  // pointer is valid until the VmPageList is destroyed or any of the Remove*/Take/Merge etc
  // functions are called.
  //
  // Lookup may return 'nullptr' if there is no slot allocated for the given offset. If non-null
  // is returned it may still be the case that IsEmpty() on the returned PageOrMarker is true.
  const VmPageOrMarker* Lookup(uint64_t offset) const;
  VmPageOrMarker* Lookup(uint64_t offset);

  // Similar to `Lookup` but only returns `nullptr` if a slot cannot be allocated either due to out
  // of memory or due to offset being invalid.
  VmPageOrMarker* LookupOrAllocate(uint64_t offset);

  // Removes any page at |offset| from the list and returns it, or VmPageOrMarker::Empty() if none.
  VmPageOrMarker RemovePage(uint64_t offset);

  // Release every page in the in the page list and calls free_page_fn on each one, giving it
  // ownership. Any markers are cleared.
  template <typename T>
  void RemoveAllPages(T free_page_fn) {
    // per page get a reference to the page pointer inside the page list node
    auto per_page_func = [&free_page_fn](VmPageOrMarker& p, uint64_t offset) {
      if (p.IsPage()) {
        free_page_fn(p.ReleasePage());
      }
      p = VmPageOrMarker::Empty();
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
    start_offset += list_skew_;
    end_offset += list_skew_;

    // Find the first node with a start after start_offset; if start_offset
    // is in a node, it'll be in the one before that one.
    auto start = --list_.upper_bound(start_offset);
    if (!start.IsValid()) {
      start = list_.begin();
    }
    // Find the first node which is completely after the end of the region. If
    // end_offset falls in the middle of a node, this finds the next node.
    const auto end = list_.lower_bound(end_offset);

    // Visitor function which moves the pages from the VmPageListNode
    // to the accumulation list.
    auto per_page_func = [&per_page_fn](VmPageOrMarker& p, uint64_t offset) {
      per_page_fn(&p, offset);
      return ZX_ERR_NEXT;
    };

    // Iterate through all nodes which have at least some overlap with the
    // region, freeing the pages and erasing nodes which become empty.
    while (start != end) {
      auto cur = start++;
      cur->ForEveryPage(per_page_func, start_offset, end_offset, list_skew_);
      if (cur->IsEmpty()) {
        list_.erase(cur);
      }
    }
  }

  // Returns true if there are no pages or markers in the page list.
  bool IsEmpty() const;

  // Returns true if the page list does not own any vm_page.
  bool HasNoPages() const;

  // Merges the pages in |other| in the range [|offset|, |end_offset|) into |this|
  // page list, starting at offset 0 in this list.
  //
  // For every page in |other| in the given range, if there is no corresponding page or marker
  // in |this|, then they will be passed to |migrate_fn|. If |migrate_fn| leaves the page in the
  // VmPageOrMarker it will be migrated into |this|, otherwise the migrate_fn is assumed to now own
  // the page. For any pages in |other| outside the given range or which conflict with a page in
  // |this|, they will be released given ownership to |release_fn|.
  //
  // The |offset| values passed to |release_fn| and |migrate_fn| are the original offsets
  // in |other|, not the adapted offsets in |this|.
  //
  // **NOTE** unlike MergeOnto, |other| will be empty at the end of this method.
  void MergeFrom(VmPageList& other, uint64_t offset, uint64_t end_offset,
                 fbl::Function<void(vm_page*, uint64_t offset)> release_fn,
                 fbl::Function<void(VmPageOrMarker*, uint64_t offset)> migrate_fn);

  // Merges this pages in |this| onto |other|.
  //
  // For every page (or marker) in |this|, checks the same offset in |other|. If there is no
  // page or marker, then it inserts the page into |other|. Otherwise, it releases the page and
  // gives ownership to |release_fn|.
  //
  // **NOTE** unlike MergeFrom, |this| will be empty at the end of this method.
  void MergeOnto(VmPageList& other, fbl::Function<void(vm_page*)> release_fn);

  // Takes the pages and markers in the range [offset, length) out of this page list.
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
      zx_status_t status = pl.ForEveryPage(
          per_page_func, pl.offset(), pl.offset() + pl.kPageFanOut * PAGE_SIZE, self->list_skew_);
      if (unlikely(status != ZX_ERR_NEXT)) {
        if (status == ZX_ERR_STOP) {
          break;
        }
        return status;
      }
    }
    return ZX_OK;
  }

  template <typename S, typename F>
  static zx_status_t ForEveryPageInRange(S self, F per_page_func, uint64_t start_offset,
                                         uint64_t end_offset) {
    start_offset += self->list_skew_;
    end_offset += self->list_skew_;

    // Find the first node with a start after start_offset; if start_offset
    // is in a node, it'll be in the one before it.
    auto start = --(self->list_.upper_bound(start_offset));
    if (!start.IsValid()) {
      start = self->list_.begin();
    }
    const auto end = self->list_.lower_bound(end_offset);
    for (auto itr = start; itr != end; ++itr) {
      auto& pl = *itr;
      zx_status_t status =
          pl.ForEveryPage(per_page_func, start_offset, end_offset, self->list_skew_);
      if (unlikely(status != ZX_ERR_NEXT)) {
        if (status == ZX_ERR_STOP) {
          break;
        }
        return status;
      }
    }
    return ZX_OK;
  }

  template <typename S, typename PAGE_FUNC, typename GAP_FUNC>
  static zx_status_t ForEveryPageAndGapInRange(S self, PAGE_FUNC per_page_func,
                                               GAP_FUNC per_gap_func, uint64_t start_offset,
                                               uint64_t end_offset) {
    uint64_t expected_next_off = start_offset;
    auto per_page_wrapper_fn = [&expected_next_off, end_offset, per_page_func, &per_gap_func](
                                   auto& p, uint64_t off) {
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

    zx_status_t status = ForEveryPageInRange(self, per_page_wrapper_fn, start_offset, end_offset);
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

  fbl::WAVLTree<uint64_t, ktl::unique_ptr<VmPageListNode>> list_;
  // A skew added to offsets provided as arguments to VmPageList functions before
  // interfacing with list_. This allows all VmPageLists within a clone tree
  // to place individual vm_page_t entries at the same offsets within their nodes, so
  // that the nodes can be moved between different lists without having to worry
  // about needing to split up a node.
  uint64_t list_skew_ = 0;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_VM_PAGE_LIST_H_
