// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "vm/vm_page_list.h"

#include <align.h>
#include <err.h>
#include <inttypes.h>
#include <trace.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <ktl/move.h>
#include <vm/pmm.h>
#include <vm/vm.h>
#include <vm/vm_object_paged.h>

#include "vm_priv.h"

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

namespace {

inline uint64_t offset_to_node_offset(uint64_t offset, uint64_t skew) {
  return ROUNDDOWN(offset + skew, PAGE_SIZE * VmPageListNode::kPageFanOut);
}

inline uint64_t offset_to_node_index(uint64_t offset, uint64_t skew) {
  return ((offset + skew) >> PAGE_SIZE_SHIFT) % VmPageListNode::kPageFanOut;
}

inline void move_vm_page_list_node(VmPageListNode* dest, VmPageListNode* src) {
  // Called by move ctor/assignment. Move assignment clears the dest node first.
  ASSERT(dest->IsEmpty());

  for (unsigned i = 0; i < VmPageListNode::kPageFanOut; i++) {
    dest->Lookup(i) = ktl::move(src->Lookup(i));
  }
}

}  // namespace

VmPageListNode::VmPageListNode(uint64_t offset) : obj_offset_(offset) {
  LTRACEF("%p offset %#" PRIx64 "\n", this, obj_offset_);
}

VmPageListNode::~VmPageListNode() {
  LTRACEF("%p offset %#" PRIx64 "\n", this, obj_offset_);
  canary_.Assert();

  DEBUG_ASSERT(HasNoPages());
}

VmPageList::VmPageList() { LTRACEF("%p\n", this); }

VmPageList::VmPageList(VmPageList&& other) : list_(ktl::move(other.list_)) {
  LTRACEF("%p\n", this);
  list_skew_ = other.list_skew_;
}

VmPageList::~VmPageList() {
  LTRACEF("%p\n", this);
  DEBUG_ASSERT(HasNoPages());
}

VmPageList& VmPageList::operator=(VmPageList&& other) {
  list_ = ktl::move(other.list_);
  list_skew_ = other.list_skew_;
  return *this;
}

VmPageOrMarker* VmPageList::LookupOrAllocate(uint64_t offset) {
  uint64_t node_offset = offset_to_node_offset(offset, list_skew_);
  size_t index = offset_to_node_index(offset, list_skew_);

  if (node_offset >= VmPageList::MAX_SIZE) {
    return nullptr;
  }

  LTRACEF_LEVEL(2, "%p offset %#" PRIx64 " node_offset %#" PRIx64 " index %zu\n", this, offset,
                node_offset, index);

  // lookup the tree node that holds this page
  auto pln = list_.find(node_offset);
  if (pln.IsValid()) {
    return &pln->Lookup(index);
  }

  fbl::AllocChecker ac;
  ktl::unique_ptr<VmPageListNode> pl =
      ktl::unique_ptr<VmPageListNode>(new (&ac) VmPageListNode(node_offset));
  if (!ac.check()) {
    return nullptr;
  }

  LTRACEF("allocating new inner node %p\n", pl.get());

  VmPageOrMarker& p = pl->Lookup(index);

  list_.insert(ktl::move(pl));
  return &p;
}

VmPageOrMarker* VmPageList::Lookup(uint64_t offset) {
  uint64_t node_offset = offset_to_node_offset(offset, list_skew_);
  size_t index = offset_to_node_index(offset, list_skew_);

  LTRACEF_LEVEL(2, "%p offset %#" PRIx64 " node_offset %#" PRIx64 " index %zu\n", this, offset,
                node_offset, index);

  // lookup the tree node that holds this page
  auto pln = list_.find(node_offset);
  if (!pln.IsValid()) {
    return nullptr;
  }

  return &pln->Lookup(index);
}

const VmPageOrMarker* VmPageList::Lookup(uint64_t offset) const {
  uint64_t node_offset = offset_to_node_offset(offset, list_skew_);
  size_t index = offset_to_node_index(offset, list_skew_);

  LTRACEF_LEVEL(2, "%p offset %#" PRIx64 " node_offset %#" PRIx64 " index %zu\n", this, offset,
                node_offset, index);

  // lookup the tree node that holds this page
  auto pln = list_.find(node_offset);
  if (!pln.IsValid()) {
    return nullptr;
  }

  return &pln->Lookup(index);
}

VmPageOrMarker VmPageList::RemovePage(uint64_t offset) {
  uint64_t node_offset = offset_to_node_offset(offset, list_skew_);
  size_t index = offset_to_node_index(offset, list_skew_);

  LTRACEF_LEVEL(2, "%p offset %#" PRIx64 " node_offset %#" PRIx64 " index %zu\n", this, offset,
                node_offset, index);

  // lookup the tree node that holds this page
  auto pln = list_.find(node_offset);
  if (!pln.IsValid()) {
    return VmPageOrMarker::Empty();
  }

  // free this page
  VmPageOrMarker page = ktl::move(pln->Lookup(index));
  if (page.IsPage() && pln->IsEmpty()) {
    // if it was the last page in the node, remove the node from the tree
    LTRACEF_LEVEL(2, "%p freeing the list node\n", this);
    list_.erase(*pln);
  }
  return page;
}

bool VmPageList::IsEmpty() const { return list_.is_empty(); }

bool VmPageList::HasNoPages() const {
  bool no_pages = true;
  ForEveryPage([&no_pages](auto* p, uint64_t) {
    if (p->IsPage()) {
      no_pages = false;
      return ZX_ERR_STOP;
    } else {
      return ZX_ERR_NEXT;
    }
  });
  return no_pages;
}

void VmPageList::MergeFrom(VmPageList& other, const uint64_t offset, const uint64_t end_offset,
                           fbl::Function<void(vm_page*, uint64_t offset)> release_fn,
                           fbl::Function<void(VmPageOrMarker*, uint64_t offset)> migrate_fn) {
  constexpr uint64_t kNodeSize = PAGE_SIZE * VmPageListNode::kPageFanOut;
  // The skewed |offset| in |other| must be equal to 0 skewed in |this|. This allows
  // nodes to moved directly between the lists, without having to worry about allocations.
  DEBUG_ASSERT((other.list_skew_ + offset) % kNodeSize == list_skew_);

  auto release_fn_wrapper = [&release_fn](VmPageOrMarker* page_or_marker, uint64_t offset) {
    if (page_or_marker->IsPage()) {
      vm_page_t* page = page_or_marker->ReleasePage();
      release_fn(page, offset);
    }
    *page_or_marker = VmPageOrMarker::Empty();
    return ZX_ERR_NEXT;
  };

  // Free pages outside of [|offset|, |end_offset|) so that the later code
  // doesn't have to worry about dealing with this.
  if (offset) {
    other.RemovePages(release_fn_wrapper, 0, offset);
  }
  other.RemovePages(release_fn_wrapper, end_offset, MAX_SIZE);

  // Calculate how much we need to shift nodes so that the node in |other| which contains
  // |offset| gets mapped to offset 0 in |this|.
  const uint64_t node_shift = offset_to_node_offset(offset, other.list_skew_);

  auto other_iter = other.list_.lower_bound(node_shift);
  while (other_iter.IsValid()) {
    uint64_t other_offset = other_iter->GetKey();
    // Any such nodes should have already been freed.
    DEBUG_ASSERT(other_offset < (end_offset + other.list_skew_));

    auto cur = other_iter++;
    auto other_node = other.list_.erase(cur);
    other_node->set_offset(other_offset - node_shift);

    auto target = list_.find(other_offset - node_shift);
    if (target.IsValid()) {
      // If there's already a node at the desired location, then merge the two nodes.
      for (unsigned i = 0; i < VmPageListNode::kPageFanOut; i++) {
        uint64_t src_offset = other_offset - other.list_skew_ + i * PAGE_SIZE;
        VmPageOrMarker page = ktl::move(other_node->Lookup(i));
        VmPageOrMarker& target_page = target->Lookup(i);
        if (target_page.IsEmpty()) {
          if (page.IsPage()) {
            migrate_fn(&page, src_offset);
          }
          target_page = ktl::move(page);
        } else if (page.IsPage()) {
          release_fn(page.ReleasePage(), src_offset);
        }

        // In all cases if we still have a page add it to the free list.
        DEBUG_ASSERT(!page.IsPage());
      }
    } else {
      // If there's no node at the desired location, then directly insert the node.
      list_.insert_or_find(ktl::move(other_node), &target);
      bool has_page = false;
      for (unsigned i = 0; i < VmPageListNode::kPageFanOut; i++) {
        VmPageOrMarker& page = target->Lookup(i);
        if (page.IsPage()) {
          migrate_fn(&page, other_offset - other.list_skew_ + i * PAGE_SIZE);
          if (page.IsPage()) {
            has_page = true;
          }
        } else if (page.IsMarker()) {
          has_page = true;
        }
      }
      if (!has_page) {
        list_.erase(target);
      }
    }
  }
}

void VmPageList::MergeOnto(VmPageList& other, fbl::Function<void(vm_page*)> release_fn) {
  DEBUG_ASSERT(other.list_skew_ == list_skew_);

  auto iter = list_.begin();
  while (iter.IsValid()) {
    auto node = list_.erase(iter++);
    auto target = other.list_.find(node->GetKey());
    if (target.IsValid()) {
      // If there's already a node at the desired location, then merge the two nodes.
      for (unsigned i = 0; i < VmPageListNode::kPageFanOut; i++) {
        VmPageOrMarker page = ktl::move(node->Lookup(i));
        if (page.IsEmpty()) {
          continue;
        }
        VmPageOrMarker& old_page = target->Lookup(i);
        VmPageOrMarker removed = ktl::move(old_page);
        old_page = ktl::move(page);
        if (removed.IsPage()) {
          release_fn(removed.ReleasePage());
        }
      }
    } else {
      other.list_.insert(ktl::move(node));
    }
  }
}

VmPageSpliceList VmPageList::TakePages(uint64_t offset, uint64_t length) {
  VmPageSpliceList res(offset, length);
  const uint64_t end = offset + length;

  // Taking pages from children isn't supported, so list_skew_ should be 0.
  DEBUG_ASSERT(list_skew_ == 0);

  // If we can't take the whole node at the start of the range,
  // the shove the pages into the splice list head_ node.
  while (offset_to_node_index(offset, 0) != 0 && offset < end) {
    res.head_.Lookup(offset_to_node_index(offset, 0)) = RemovePage(offset);
    offset += PAGE_SIZE;
  }

  // As long as the current and end node offsets are different, we
  // can just move the whole node into the splice list.
  while (offset_to_node_offset(offset, 0) != offset_to_node_offset(end, 0)) {
    ktl::unique_ptr<VmPageListNode> node = list_.erase(offset_to_node_offset(offset, 0));
    if (node) {
      res.middle_.insert(ktl::move(node));
    }
    offset += (PAGE_SIZE * VmPageListNode::kPageFanOut);
  }

  // Move any remaining pages into the splice list tail_ node.
  while (offset < end) {
    res.tail_.Lookup(offset_to_node_index(offset, 0)) = RemovePage(offset);
    offset += PAGE_SIZE;
  }

  return res;
}

VmPageSpliceList::VmPageSpliceList() : VmPageSpliceList(0, 0) {}

VmPageSpliceList::VmPageSpliceList(uint64_t offset, uint64_t length)
    : offset_(offset), length_(length) {}

VmPageSpliceList::VmPageSpliceList(VmPageSpliceList&& other)
    : offset_(other.offset_),
      length_(other.length_),
      pos_(other.pos_),
      middle_(ktl::move(other.middle_)) {
  move_vm_page_list_node(&head_, &other.head_);
  move_vm_page_list_node(&tail_, &other.tail_);

  other.offset_ = other.length_ = other.pos_ = 0;
}

VmPageSpliceList& VmPageSpliceList::operator=(VmPageSpliceList&& other) {
  FreeAllPages();

  offset_ = other.offset_;
  length_ = other.length_;
  pos_ = other.pos_;
  move_vm_page_list_node(&head_, &other.head_);
  move_vm_page_list_node(&tail_, &other.tail_);
  middle_ = ktl::move(other.middle_);

  other.offset_ = other.length_ = other.pos_ = 0;

  return *this;
}

VmPageSpliceList::~VmPageSpliceList() { FreeAllPages(); }

void VmPageSpliceList::FreeAllPages() {
  // Free any pages owned by the splice list.
  while (!IsDone()) {
    VmPageOrMarker page = Pop();
    if (page.IsPage()) {
      pmm_free_page(page.ReleasePage());
    }
  }
}

VmPageOrMarker VmPageSpliceList::Pop() {
  if (IsDone()) {
    DEBUG_ASSERT_MSG(false, "Popped from empty splice list");
    return VmPageOrMarker::Empty();
  }

  const uint64_t cur_offset = offset_ + pos_;
  const auto cur_node_idx = offset_to_node_index(cur_offset, 0);
  const auto cur_node_offset = offset_to_node_offset(cur_offset, 0);

  VmPageOrMarker res;
  if (offset_to_node_index(offset_, 0) != 0 &&
      offset_to_node_offset(offset_, 0) == cur_node_offset) {
    // If the original offset means that pages were placed in head_
    // and the current offset points to the same node, look there.
    res = ktl::move(head_.Lookup(cur_node_idx));
  } else if (cur_node_offset != offset_to_node_offset(offset_ + length_, 0)) {
    // If the current offset isn't pointing to the tail node,
    // look in the middle tree.
    auto middle_node = middle_.find(cur_node_offset);
    if (middle_node.IsValid()) {
      res = ktl::move(middle_node->Lookup(cur_node_idx));
    }
  } else {
    // If none of the other cases, we're in the tail_.
    res = ktl::move(tail_.Lookup(cur_node_idx));
  }

  pos_ += PAGE_SIZE;
  return res;
}
