// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <vm/vm_page_list.h>

#include <err.h>
#include <fbl/alloc_checker.h>
#include <inttypes.h>
#include <ktl/move.h>
#include <trace.h>
#include <vm/pmm.h>
#include <vm/vm.h>
#include <vm/vm_object_paged.h>
#include <zircon/types.h>

#include "vm_priv.h"

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

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
        vm_page* page = src->RemovePage(i);
        if (page) {
            dest->AddPage(page, i);
        }
    }
}

} // namespace

VmPageListNode::VmPageListNode(uint64_t offset)
    : obj_offset_(offset) {
    LTRACEF("%p offset %#" PRIx64 "\n", this, obj_offset_);
}

VmPageListNode::~VmPageListNode() {
    LTRACEF("%p offset %#" PRIx64 "\n", this, obj_offset_);
    canary_.Assert();

    for (__UNUSED auto p : pages_) {
        DEBUG_ASSERT(p == nullptr);
    }
}

vm_page* VmPageListNode::GetPage(size_t index) const {
    canary_.Assert();
    DEBUG_ASSERT(index < kPageFanOut);
    return pages_[index];
}

vm_page* VmPageListNode::RemovePage(size_t index) {
    canary_.Assert();
    DEBUG_ASSERT(index < kPageFanOut);

    auto p = pages_[index];
    if (!p) {
        return nullptr;
    }

    pages_[index] = nullptr;

    return p;
}

zx_status_t VmPageListNode::AddPage(vm_page* p, size_t index) {
    canary_.Assert();
    DEBUG_ASSERT(index < kPageFanOut);
    if (pages_[index]) {
        return ZX_ERR_ALREADY_EXISTS;
    }
    pages_[index] = p;
    return ZX_OK;
}

VmPageList::VmPageList() {
    LTRACEF("%p\n", this);
}

VmPageList::VmPageList(VmPageList&& other) : list_(std::move(other.list_)) {
    LTRACEF("%p\n", this);
    list_skew_ = other.list_skew_;
}

VmPageList::~VmPageList() {
    LTRACEF("%p\n", this);
    DEBUG_ASSERT(list_.is_empty());
}

VmPageList& VmPageList::operator=(VmPageList&& other) {
    list_ = std::move(other.list_);
    list_skew_ = other.list_skew_;
    return *this;
}

zx_status_t VmPageList::AddPage(vm_page* p, uint64_t offset) {
    uint64_t node_offset = offset_to_node_offset(offset, list_skew_);
    size_t index = offset_to_node_index(offset, list_skew_);

    if (node_offset >= VmObjectPaged::MAX_SIZE) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    LTRACEF_LEVEL(2, "%p page %p, offset %#" PRIx64 " node_offset %#" PRIx64 " index %zu\n", this, p, offset,
                  node_offset, index);

    // lookup the tree node that holds this page
    auto pln = list_.find(node_offset);
    if (!pln.IsValid()) {
        fbl::AllocChecker ac;
        ktl::unique_ptr<VmPageListNode> pl =
            ktl::unique_ptr<VmPageListNode>(new (&ac) VmPageListNode(node_offset));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        LTRACEF("allocating new inner node %p\n", pl.get());
        __UNUSED auto status = pl->AddPage(p, index);
        DEBUG_ASSERT(status == ZX_OK);

        list_.insert(ktl::move(pl));
        return ZX_OK;
    } else {
        return pln->AddPage(p, index);
    }
}

vm_page* VmPageList::GetPage(uint64_t offset) const {
    uint64_t node_offset = offset_to_node_offset(offset, list_skew_);
    size_t index = offset_to_node_index(offset, list_skew_);

    LTRACEF_LEVEL(2, "%p offset %#" PRIx64 " node_offset %#" PRIx64 " index %zu\n", this, offset, node_offset,
                  index);

    // lookup the tree node that holds this page
    auto pln = list_.find(node_offset);
    if (!pln.IsValid()) {
        return nullptr;
    }

    return pln->GetPage(index);
}

bool VmPageList::RemovePage(uint64_t offset, vm_page_t** page_out) {
    DEBUG_ASSERT(page_out);

    uint64_t node_offset = offset_to_node_offset(offset, list_skew_);
    size_t index = offset_to_node_index(offset, list_skew_);

    LTRACEF_LEVEL(2, "%p offset %#" PRIx64 " node_offset %#" PRIx64 " index %zu\n", this, offset, node_offset,
                  index);

    // lookup the tree node that holds this page
    auto pln = list_.find(node_offset);
    if (!pln.IsValid()) {
        return false;
    }

    // free this page
    auto page = pln->RemovePage(index);
    if (page) {
        // if it was the last page in the node, remove the node from the tree
        if (pln->IsEmpty()) {
            LTRACEF_LEVEL(2, "%p freeing the list node\n", this);
            list_.erase(*pln);
        }

        *page_out = page;
        return true;
    } else {
        return false;
    }
}

void VmPageList::RemovePages(uint64_t start_offset, uint64_t end_offset,
                             list_node_t* removed_pages) {
    RemovePages([](vm_page_t*& p, uint64_t offset) -> bool { return true; },
            start_offset, end_offset, removed_pages);
}

size_t VmPageList::RemoveAllPages(list_node_t* removed_pages) {
    LTRACEF("%p\n", this);

    DEBUG_ASSERT(removed_pages);

    size_t count = 0;

    // per page get a reference to the page pointer inside the page list node
    auto per_page_func = [&](vm_page*& p, uint64_t offset) {

        // add the page to our list and null out the inner node
        list_add_tail(removed_pages, &p->queue_node);
        p = nullptr;
        count++;
        return ZX_ERR_NEXT;
    };

    // walk the tree in order, freeing all the pages on every node
    ForEveryPage(per_page_func);

    // empty the tree
    list_.clear();

    return count;
}

bool VmPageList::IsEmpty() {
    return list_.is_empty();
}

void VmPageList::MergeFrom(VmPageList& other, const uint64_t offset, const uint64_t end_offset,
                           fbl::Function<void(vm_page*, uint64_t offset)> release_fn,
                           fbl::Function<void(vm_page*, uint64_t offset)> migrate_fn,
                           list_node_t* free_list) {
    constexpr uint64_t kNodeSize = PAGE_SIZE * VmPageListNode::kPageFanOut;
    // The skewed |offset| in |other| must be equal to 0 skewed in |this|. This allows
    // nodes to moved directly between the lists, without having to worry about allocations.
    DEBUG_ASSERT((other.list_skew_ + offset) % kNodeSize == list_skew_);

    auto release_fn_wrapper = [&release_fn](vm_page_t*& p, uint64_t offset) -> bool {
        release_fn(p, offset);
        return true;
    };

    // Free pages outside of [|offset|, |end_offset|) so that the later code
    // doesn't have to worry about dealing with this.
    if (offset) {
        other.RemovePages(release_fn_wrapper, 0, offset, free_list);
    }
    other.RemovePages(release_fn_wrapper, end_offset, MAX_SIZE, free_list);

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
            // If there'a already a node at the desired location, then merge the two nodes.
            for (unsigned i = 0; i < VmPageListNode::kPageFanOut; i++) {
                auto page = other_node->RemovePage(i);
                if (!page) {
                    continue;
                }

                zx_status_t status = target->AddPage(page, i);
                uint64_t src_offset = other_offset - other.list_skew_ + i * PAGE_SIZE;
                if (status == ZX_OK) {
                    migrate_fn(page, src_offset);
                } else {
                    release_fn(page, src_offset);
                    list_add_tail(free_list, &page->queue_node);
                }
            }
        } else {
            // If there's no node at the desired location, then directly insert the node.
            list_.insert_or_find(ktl::move(other_node), &target);
            for (unsigned i = 0; i < VmPageListNode::kPageFanOut; i++) {
                auto page = target->GetPage(i);
                if (page) {
                    migrate_fn(page, other_offset - other.list_skew_ + i * PAGE_SIZE);
                }
            }
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
        vm_page_t* page;
        if (RemovePage(offset, &page)) {
            res.head_.AddPage(page, offset_to_node_index(offset, 0));
        }
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
        vm_page_t* page;
        if (RemovePage(offset, &page)) {
            res.tail_.AddPage(page, offset_to_node_index(offset, 0));
        }
        offset += PAGE_SIZE;
    }

    return res;
}

VmPageSpliceList::VmPageSpliceList() : VmPageSpliceList(0, 0) {}

VmPageSpliceList::VmPageSpliceList(uint64_t offset, uint64_t length)
    : offset_(offset), length_(length) {
}

VmPageSpliceList::VmPageSpliceList(VmPageSpliceList&& other)
    : offset_(other.offset_), length_(other.length_),
      pos_(other.pos_), middle_(ktl::move(other.middle_)) {
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

VmPageSpliceList::~VmPageSpliceList() {
    FreeAllPages();
}

void VmPageSpliceList::FreeAllPages() {
    // Free any pages owned by the splice list.
    while (!IsDone()) {
        auto page = Pop();
        if (page) {
            pmm_free_page(page);
        }
    }
}

vm_page* VmPageSpliceList::Pop() {
    if (IsDone()) {
        DEBUG_ASSERT_MSG(false, "Popped from empty splice list");
        return nullptr;
    }

    const uint64_t cur_offset = offset_ + pos_;
    const auto cur_node_idx = offset_to_node_index(cur_offset, 0);
    const auto cur_node_offset = offset_to_node_offset(cur_offset, 0);

    vm_page* res;
    if (offset_to_node_index(offset_, 0) != 0
            && offset_to_node_offset(offset_, 0) == cur_node_offset) {
        // If the original offset means that pages were placed in head_
        // and the current offset points to the same node, look there.
        res = head_.RemovePage(cur_node_idx);
    } else if (cur_node_offset != offset_to_node_offset(offset_ + length_, 0)) {
        // If the current offset isn't pointing to the tail node,
        // look in the middle tree.
        auto middle_node = middle_.find(cur_node_offset);
        if (middle_node.IsValid()) {
            res = middle_node->RemovePage(cur_node_idx);
        } else {
            res = nullptr;
        }
    } else {
        // If none of the other cases, we're in the tail_.
        res = tail_.RemovePage(cur_node_idx);
    }

    pos_ += PAGE_SIZE;
    return res;
}
