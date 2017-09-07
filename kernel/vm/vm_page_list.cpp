// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <vm/vm_page_list.h>

#include <err.h>
#include <inttypes.h>
#include <kernel/vm.h>
#include <fbl/alloc_checker.h>
#include <trace.h>
#include <vm/pmm.h>

#include "vm_priv.h"

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

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

vm_page* VmPageListNode::GetPage(size_t index) {
    canary_.Assert();
    DEBUG_ASSERT(index < kPageFanOut);
    return pages_[index];
}

vm_page* VmPageListNode::RemovePage(size_t index) {
    canary_.Assert();
    DEBUG_ASSERT(index < kPageFanOut);

    auto p = pages_[index];
    if (!p)
        return nullptr;

    pages_[index] = nullptr;

    return p;
}

status_t VmPageListNode::AddPage(vm_page* p, size_t index) {
    canary_.Assert();
    DEBUG_ASSERT(index < kPageFanOut);
    if (pages_[index])
        return MX_ERR_ALREADY_EXISTS;
    pages_[index] = p;
    return MX_OK;
}

VmPageList::VmPageList() {
    LTRACEF("%p\n", this);
}

VmPageList::~VmPageList() {
    LTRACEF("%p\n", this);
    DEBUG_ASSERT(list_.is_empty());
}

status_t VmPageList::AddPage(vm_page* p, uint64_t offset) {
    uint64_t node_offset = ROUNDDOWN(offset, PAGE_SIZE * VmPageListNode::kPageFanOut);
    size_t index = (offset >> PAGE_SIZE_SHIFT) % VmPageListNode::kPageFanOut;

    LTRACEF_LEVEL(2, "%p page %p, offset %#" PRIx64 " node_offset %#" PRIx64 " index %zu\n", this, p, offset,
                  node_offset, index);

    // lookup the tree node that holds this page
    auto pln = list_.find(node_offset);
    if (!pln.IsValid()) {
        fbl::AllocChecker ac;
        fbl::unique_ptr<VmPageListNode> pl =
            fbl::unique_ptr<VmPageListNode>(new (&ac) VmPageListNode(node_offset));
        if (!ac.check())
            return MX_ERR_NO_MEMORY;

        LTRACEF("allocating new inner node %p\n", pl.get());
        __UNUSED auto status = pl->AddPage(p, index);
        DEBUG_ASSERT(status == MX_OK);

        list_.insert(fbl::move(pl));
    } else {
        pln->AddPage(p, index);
    }

    return MX_OK;
}

vm_page* VmPageList::GetPage(uint64_t offset) {
    uint64_t node_offset = ROUNDDOWN(offset, PAGE_SIZE * VmPageListNode::kPageFanOut);
    size_t index = (offset >> PAGE_SIZE_SHIFT) % VmPageListNode::kPageFanOut;

    LTRACEF_LEVEL(2, "%p offset %#" PRIx64 " node_offset %#" PRIx64 " index %zu\n", this, offset, node_offset,
                  index);

    // lookup the tree node that holds this page
    auto pln = list_.find(node_offset);
    if (!pln.IsValid()) {
        return nullptr;
    }

    return pln->GetPage(index);
}

status_t VmPageList::FreePage(uint64_t offset) {
    uint64_t node_offset = ROUNDDOWN(offset, PAGE_SIZE * VmPageListNode::kPageFanOut);
    size_t index = (offset >> PAGE_SIZE_SHIFT) % VmPageListNode::kPageFanOut;

    LTRACEF_LEVEL(2, "%p offset %#" PRIx64 " node_offset %#" PRIx64 " index %zu\n", this, offset, node_offset,
                  index);

    // lookup the tree node that holds this page
    auto pln = list_.find(node_offset);
    if (!pln.IsValid()) {
        return MX_ERR_NOT_FOUND;
    }

    // free this page
    auto page = pln->RemovePage(index);
    if (page) {
        // if it was the last page in the node, remove the node from the tree
        if (pln->IsEmpty()) {
            LTRACEF_LEVEL(2, "%p freeing the list node\n", this);
            list_.erase(*pln);
        }

        pmm_free_page(page);
    }

    return MX_OK;
}

size_t VmPageList::FreeAllPages() {
    LTRACEF("%p\n", this);

    list_node list;
    list_initialize(&list);

    size_t count = 0;

    // per page get a reference to the page pointer inside the page list node
    auto per_page_func = [&](vm_page*& p, uint64_t offset) {
        // add the page to our list and null out the inner node
        list_add_tail(&list, &p->free.node);
        p = nullptr;
        count++;
        return MX_ERR_NEXT;
    };

    // walk the tree in order, freeing all the pages on every node
    ForEveryPage(per_page_func);

    // return all the pages to the pmm at once
    __UNUSED auto freed = pmm_free(&list);
    DEBUG_ASSERT(freed == count);

    // empty the tree
    list_.clear();

    return count;
}
