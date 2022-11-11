// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "vm/vm_page_list.h"

#include <align.h>
#include <inttypes.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <ktl/move.h>
#include <vm/pmm.h>
#include <vm/vm.h>
#include <vm/vm_object_paged.h>

#include "vm_priv.h"

#include <ktl/enforce.h>

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

  DEBUG_ASSERT(HasNoPageOrRef());
}

VmPageList::VmPageList() { LTRACEF("%p\n", this); }

VmPageList::VmPageList(VmPageList&& other) : list_(ktl::move(other.list_)) {
  LTRACEF("%p\n", this);
  list_skew_ = other.list_skew_;
}

VmPageList::~VmPageList() {
  LTRACEF("%p\n", this);
  DEBUG_ASSERT(HasNoPageOrRef());
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

void VmPageList::ReturnEmptySlot(uint64_t offset) {
  uint64_t node_offset = offset_to_node_offset(offset, list_skew_);
  size_t index = offset_to_node_index(offset, list_skew_);

  LTRACEF_LEVEL(2, "%p offset %#" PRIx64 " node_offset %#" PRIx64 " index %zu\n", this, offset,
                node_offset, index);

  // lookup the tree node that holds this offset
  auto pln = list_.find(node_offset);
  DEBUG_ASSERT(pln.IsValid());

  // check that the slot was empty
  [[maybe_unused]] VmPageOrMarker page = ktl::move(pln->Lookup(index));
  DEBUG_ASSERT(page.IsEmpty());
  if (pln->IsEmpty()) {
    // node is empty, erase it.
    list_.erase(*pln);
  }
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

VmPageOrMarkerRef VmPageList::LookupMutable(uint64_t offset) {
  uint64_t node_offset = offset_to_node_offset(offset, list_skew_);
  size_t index = offset_to_node_index(offset, list_skew_);

  LTRACEF_LEVEL(2, "%p offset %#" PRIx64 " node_offset %#" PRIx64 " index %zu\n", this, offset,
                node_offset, index);

  // lookup the tree node that holds this page
  auto pln = list_.find(node_offset);
  if (!pln.IsValid()) {
    return VmPageOrMarkerRef(nullptr);
  }

  return VmPageOrMarkerRef(&pln->Lookup(index));
}

VmPageOrMarker VmPageList::RemoveContent(uint64_t offset) {
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
  if (!page.IsEmpty() && pln->IsEmpty()) {
    // if it was the last item in the node, remove the node from the tree
    LTRACEF_LEVEL(2, "%p freeing the list node\n", this);
    list_.erase(*pln);
  }
  return page;
}

bool VmPageList::IsEmpty() const { return list_.is_empty(); }

bool VmPageList::HasNoPageOrRef() const {
  bool no_pages = true;
  ForEveryPage([&no_pages](auto* p, uint64_t) {
    if (p->IsPageOrRef()) {
      no_pages = false;
      return ZX_ERR_STOP;
    } else {
      return ZX_ERR_NEXT;
    }
  });
  return no_pages;
}

void VmPageList::MergeFrom(
    VmPageList& other, const uint64_t offset, const uint64_t end_offset,
    fit::inline_function<void(VmPageOrMarker&&, uint64_t offset), 3 * sizeof(void*)> release_fn,
    fit::inline_function<void(VmPageOrMarker*, uint64_t offset)> migrate_fn) {
  constexpr uint64_t kNodeSize = PAGE_SIZE * VmPageListNode::kPageFanOut;
  // The skewed |offset| in |other| must be equal to 0 skewed in |this|. This allows
  // nodes to moved directly between the lists, without having to worry about allocations.
  DEBUG_ASSERT((other.list_skew_ + offset) % kNodeSize == list_skew_);

  auto release_fn_wrapper = [&release_fn](VmPageOrMarker* page_or_marker, uint64_t offset) {
    if (!page_or_marker->IsEmpty()) {
      release_fn(ktl::move(*page_or_marker), offset);
    }
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
          if (page.IsPageOrRef()) {
            migrate_fn(&page, src_offset);
          }
          target_page = ktl::move(page);
        } else if (!page.IsEmpty()) {
          release_fn(ktl::move(page), src_offset);
        }

        // In all cases if we still have a page add it to the free list.
        DEBUG_ASSERT(!page.IsPageOrRef());
      }
    } else {
      // If there's no node at the desired location, then directly insert the node.
      list_.insert_or_find(ktl::move(other_node), &target);
      bool has_page = false;
      for (unsigned i = 0; i < VmPageListNode::kPageFanOut; i++) {
        VmPageOrMarker& page = target->Lookup(i);
        if (page.IsPageOrRef()) {
          migrate_fn(&page, other_offset - other.list_skew_ + i * PAGE_SIZE);
          if (page.IsPageOrRef()) {
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

void VmPageList::MergeOnto(VmPageList& other,
                           fit::inline_function<void(VmPageOrMarker&&)> release_fn) {
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
        if (!removed.IsEmpty()) {
          release_fn(ktl::move(removed));
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
    res.head_.Lookup(offset_to_node_index(offset, 0)) = RemoveContent(offset);
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
    res.tail_.Lookup(offset_to_node_index(offset, 0)) = RemoveContent(offset);
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

// static
VmPageSpliceList VmPageSpliceList::CreateFromPageList(uint64_t offset, uint64_t length,
                                                      list_node* pages) {
  // TODO(fxbug.dev/88859): This method needs coverage in vmpl_unittests.
  DEBUG_ASSERT(pages);
  DEBUG_ASSERT(list_length(pages) == length / PAGE_SIZE);
  VmPageSpliceList res(offset, length);
  DEBUG_ASSERT(list_is_empty(&res.raw_pages_));
  list_move(pages, &res.raw_pages_);
  return res;
}

void VmPageSpliceList::FreeAllPages() {
  // Free any pages owned by the splice list.
  while (!IsDone()) {
    VmPageOrMarker page = Pop();
    if (page.IsPage()) {
      pmm_free_page(page.ReleasePage());
    } else if (page.IsReference()) {
      // TODO(fxbug.dev/60238): Implement this once compressed pages are supported and Reference
      // types can be generated.
      panic("Reference should never be generated.");
    }
  }
}

VmPageOrMarker VmPageSpliceList::Pop() {
  if (IsDone()) {
    DEBUG_ASSERT_MSG(false, "Popped from empty splice list");
    return VmPageOrMarker::Empty();
  }

  VmPageOrMarker res;
  if (!list_is_empty(&raw_pages_)) {
    // TODO(fxbug.dev/88859): This path and CreateFromPageList() need coverage in vmpl_unittests.
    vm_page_t* head = list_remove_head_type(&raw_pages_, vm_page, queue_node);
    res = VmPageOrMarker::Page(head);
  } else {
    const uint64_t cur_offset = offset_ + pos_;
    const auto cur_node_idx = offset_to_node_index(cur_offset, 0);
    const auto cur_node_offset = offset_to_node_offset(cur_offset, 0);

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
  }

  pos_ += PAGE_SIZE;
  return res;
}
