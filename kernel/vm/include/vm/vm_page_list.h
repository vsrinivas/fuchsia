// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <kernel/vm.h>
#include <fbl/canary.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>

struct vm_page;

class VmPageListNode final : public fbl::WAVLTreeContainable<fbl::unique_ptr<VmPageListNode>> {
public:
    explicit VmPageListNode(uint64_t offset);
    ~VmPageListNode();

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmPageListNode);

    static const size_t kPageFanOut = 16;

    // accessors
    uint64_t offset() const { return obj_offset_; }
    uint64_t GetKey() const { return obj_offset_; }

    // for every valid page in the node call the passed in function
    template <typename T>
    status_t ForEveryPage(T func, uint64_t start_offset, uint64_t end_offset) {
        DEBUG_ASSERT(IS_PAGE_ALIGNED(start_offset) && IS_PAGE_ALIGNED(end_offset));
        size_t start = 0;
        size_t end = kPageFanOut;
        if (start_offset > obj_offset_) {
            start = (start_offset - obj_offset_) / PAGE_SIZE;
        }
        if (end_offset < obj_offset_) {
            return MX_ERR_NEXT;
        }
        if (end_offset < obj_offset_ + kPageFanOut * PAGE_SIZE) {
            end = (end_offset - obj_offset_) / PAGE_SIZE;
        }
        for (size_t i = start; i < end; i++) {
            if (pages_[i]) {
                status_t status = func(pages_[i], obj_offset_ + i * PAGE_SIZE);
                if (unlikely(status != MX_ERR_NEXT)) {
                    return status;
                }
            }
        }
        return MX_ERR_NEXT;
    }

    // for every valid page in the node call the passed in function
    template <typename T>
    status_t ForEveryPage(T func, uint64_t start_offset, uint64_t end_offset) const {
        DEBUG_ASSERT(IS_PAGE_ALIGNED(start_offset) && IS_PAGE_ALIGNED(end_offset));
        size_t start = 0;
        size_t end = kPageFanOut;
        if (start_offset > obj_offset_) {
            start = (start_offset - obj_offset_) / PAGE_SIZE;
        }
        if (end_offset < obj_offset_) {
            return MX_ERR_NEXT;
        }
        if (end_offset < obj_offset_ + kPageFanOut * PAGE_SIZE) {
            end = (end_offset - obj_offset_) / PAGE_SIZE;
        }
        for (size_t i = start; i < end; i++) {
            if (pages_[i]) {
                status_t status = func(pages_[i], obj_offset_ + i * PAGE_SIZE);
                if (unlikely(status != MX_ERR_NEXT)) {
                    return status;
                }
            }
        }
        return MX_ERR_NEXT;
    }

    vm_page* GetPage(size_t index);
    vm_page* RemovePage(size_t index);
    status_t AddPage(vm_page* p, size_t index);

    bool IsEmpty() const {
        for (const auto p : pages_) {
            if (p)
                return false;
        }
        return true;
    }

private:
    fbl::Canary<fbl::magic("PLST")> canary_;

    uint64_t obj_offset_ = 0;
    vm_page* pages_[kPageFanOut] = {};
};

class VmPageList final {
public:
    VmPageList();
    ~VmPageList();

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmPageList);

    // walk the page tree, calling the passed in function on every tree node
    template <typename T>
    status_t ForEveryPage(T per_page_func) {
        for (auto& pl : list_) {
            status_t status = pl.ForEveryPage(per_page_func, pl.offset(),
                                              pl.offset() + pl.kPageFanOut * PAGE_SIZE);
            if (unlikely(status != MX_ERR_NEXT)) {
                if (status == MX_ERR_STOP) {
                    break;
                }
                return status;
            }
        }
        return MX_OK;
    }

    // walk the page tree, calling the passed in function on every tree node
    template <typename T>
    status_t ForEveryPage(T per_page_func) const {
        for (auto& pl : list_) {
            status_t status = pl.ForEveryPage(per_page_func, pl.offset(),
                                              pl.offset() + pl.kPageFanOut * PAGE_SIZE);
            if (unlikely(status != MX_ERR_NEXT)) {
                if (status == MX_ERR_STOP) {
                    break;
                }
                return status;
            }
        }
        return MX_OK;
    }

    // walk the page tree, calling the passed in function on every tree node
    template <typename T>
    status_t ForEveryPageInRange(T per_page_func, uint64_t start_offset, uint64_t end_offset) {
        DEBUG_ASSERT(IS_PAGE_ALIGNED(start_offset) && IS_PAGE_ALIGNED(end_offset));
        // Find the first node with a start after start_offset; if start_offset
        // is in a node, it'll be in the one before it.
        auto start = --list_.upper_bound(start_offset);
        if (!start.IsValid()) {
            start = list_.begin();
        }
        const auto end = list_.lower_bound(end_offset);
        for (auto itr = start; itr != end; ++itr) {
            auto& pl = *itr;
            status_t status = pl.ForEveryPage(per_page_func, start_offset, end_offset);
            if (unlikely(status != MX_ERR_NEXT)) {
                if (status == MX_ERR_STOP) {
                    break;
                }
                return status;
            }
        }
        return MX_OK;
    }

    template <typename T>
    status_t ForEveryPageInRange(T per_page_func, uint64_t start_offset,
                                 uint64_t end_offset) const {
        DEBUG_ASSERT(IS_PAGE_ALIGNED(start_offset) && IS_PAGE_ALIGNED(end_offset));
        // Find the first node with a start after start_offset; if start_offset
        // is in a node, it'll be in the one before it.
        auto start = --list_.upper_bound(start_offset);
        if (!start.IsValid()) {
            start = list_.begin();
        }
        const auto end = list_.lower_bound(end_offset);
        for (auto itr = start; itr != end; ++itr) {
            auto& pl = *itr;
            status_t status = pl.ForEveryPage(per_page_func, start_offset, end_offset);
            if (unlikely(status != MX_ERR_NEXT)) {
                if (status == MX_ERR_STOP) {
                    break;
                }
                return status;
            }
        }
        return MX_OK;
    }

    status_t AddPage(vm_page*, uint64_t offset);
    vm_page* GetPage(uint64_t offset);
    status_t FreePage(uint64_t offset);
    size_t FreeAllPages();

private:
    fbl::WAVLTree<uint64_t, fbl::unique_ptr<VmPageListNode>> list_;
};
