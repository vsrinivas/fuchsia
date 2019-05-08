// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <fbl/canary.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <ktl/unique_ptr.h>
#include <vm/vm.h>
#include <zircon/types.h>

struct vm_page;

class VmPageListNode final : public fbl::WAVLTreeContainable<ktl::unique_ptr<VmPageListNode>> {
public:
    explicit VmPageListNode(uint64_t offset);
    ~VmPageListNode();

    DISALLOW_COPY_ASSIGN_AND_MOVE(VmPageListNode);

    static const size_t kPageFanOut = 16;

    // accessors
    uint64_t offset() const { return obj_offset_; }
    uint64_t GetKey() const { return obj_offset_; }

    // for every valid page in the node call the passed in function
    template <typename F>
    zx_status_t ForEveryPage(F func, uint64_t start_offset, uint64_t end_offset) {
        return ForEveryPage(this, func, start_offset, end_offset);
    }

    // for every valid page in the node call the passed in function
    template <typename F>
    zx_status_t ForEveryPage(F func, uint64_t start_offset, uint64_t end_offset) const {
        return ForEveryPage(this, func, start_offset, end_offset);
    }

    vm_page* GetPage(size_t index);
    vm_page* RemovePage(size_t index);
    zx_status_t AddPage(vm_page* p, size_t index);

    bool IsEmpty() const {
        for (const auto p : pages_) {
            if (p) {
                return false;
            }
        }
        return true;
    }

private:
    template <typename S, typename F>
    static zx_status_t ForEveryPage(S self, F func, uint64_t start_offset, uint64_t end_offset) {
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
            if (self->pages_[i]) {
                zx_status_t status = func(self->pages_[i], self->obj_offset_ + i * PAGE_SIZE);
                if (unlikely(status != ZX_ERR_NEXT)) {
                    return status;
                }
            }
        }
        return ZX_ERR_NEXT;
    }

    fbl::Canary<fbl::magic("PLST")> canary_;

    uint64_t obj_offset_ = 0;
    vm_page* pages_[kPageFanOut] = {};
};

class VmPageList;

// Class which holds the list of vm_page structs removed from a VmPageList
// by TakePages. The list include information about uncommitted pages.
class VmPageSpliceList final {
public:
    VmPageSpliceList();
    VmPageSpliceList(VmPageSpliceList&& other);
    VmPageSpliceList& operator=(VmPageSpliceList&& other_tree);
    ~VmPageSpliceList();

    // Pops the next page off of the splice. If the next page was
    // not committed, returns null.
    vm_page* Pop();

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

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(VmPageList);

    // walk the page tree, calling the passed in function on every tree node
    template <typename F>
    zx_status_t ForEveryPage(F per_page_func) {
        return ForEveryPage(this, per_page_func);
    }

    // walk the page tree, calling the passed in function on every tree node
    template <typename F>
    zx_status_t ForEveryPage(F per_page_func) const {
        return ForEveryPage(this, per_page_func);
    }

    // walk the page tree, calling the passed in function on every tree node
    template <typename F>
    zx_status_t ForEveryPageInRange(F per_page_func,
                                    uint64_t start_offset, uint64_t end_offset) {
        return ForEveryPageInRange(this, per_page_func, start_offset, end_offset);
    }

    // walk the page tree, calling the passed in function on every tree node
    template <typename F>
    zx_status_t ForEveryPageInRange(F per_page_func,
                                    uint64_t start_offset, uint64_t end_offset) const {
        return ForEveryPageInRange(this, per_page_func, start_offset, end_offset);
    }

    // walk the page tree, calling |per_page_func| on every page and |per_gap_func| on every gap
    template <typename PAGE_FUNC, typename GAP_FUNC>
    zx_status_t ForEveryPageAndGapInRange(PAGE_FUNC per_page_func, GAP_FUNC per_gap_func,
                                          uint64_t start_offset, uint64_t end_offset) {
        return ForEveryPageAndGapInRange(this, per_page_func, per_gap_func,
                                         start_offset, end_offset);
    }

    // walk the page tree, calling |per_page_func| on every page and |per_gap_func| on every gap
    template <typename PAGE_FUNC, typename GAP_FUNC>
    zx_status_t ForEveryPageAndGapInRange(PAGE_FUNC per_page_func, GAP_FUNC per_gap_func,
                                          uint64_t start_offset, uint64_t end_offset) const {
        return ForEveryPageAndGapInRange(this, per_page_func, per_gap_func,
                                         start_offset, end_offset);
    }

    zx_status_t AddPage(vm_page*, uint64_t offset);
    vm_page* GetPage(uint64_t offset);
    // Removes the page at |offset| from the list. Returns true if a page was present,
    // false otherwise. If a page was removed, returns it in |page|. The caller now owns
    // the pages.
    bool RemovePage(uint64_t offset, vm_page** page);
    // Removes all pages from this list and puts them on |removed_pages|. The caller
    // now owns the pages.
    size_t RemoveAllPages(list_node_t* removed_pages);
    // Removes all pages in the range [start_offset, end_offset) and puts them
    // on |removed_pages|. The caller now owns the pages.
    void RemovePages(uint64_t start_offset, uint64_t end_offset, list_node_t* remove_page);
    bool IsEmpty();

    // Takes the pages in the range [offset, length) out of this page list.
    VmPageSpliceList TakePages(uint64_t offset, uint64_t length);

private:
    template <typename S, typename F>
    static zx_status_t ForEveryPage(S self, F per_page_func) {
        for (auto& pl : self->list_) {
            zx_status_t status = pl.ForEveryPage(per_page_func, pl.offset(),
                                                 pl.offset() + pl.kPageFanOut * PAGE_SIZE);
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
    static zx_status_t ForEveryPageInRange(S self, F per_page_func,
                                           uint64_t start_offset, uint64_t end_offset) {
        // Find the first node with a start after start_offset; if start_offset
        // is in a node, it'll be in the one before it.
        auto start = --(self->list_.upper_bound(start_offset));
        if (!start.IsValid()) {
            start = self->list_.begin();
        }
        const auto end = self->list_.lower_bound(end_offset);
        for (auto itr = start; itr != end; ++itr) {
            auto& pl = *itr;
            zx_status_t status = pl.ForEveryPage(per_page_func, start_offset, end_offset);
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
    static zx_status_t ForEveryPageAndGapInRange(S self,
                                                 PAGE_FUNC per_page_func, GAP_FUNC per_gap_func,
                                                 uint64_t start_offset, uint64_t end_offset) {
        uint64_t expected_next_off = start_offset;
        auto per_page_wrapper_fn = [&expected_next_off, end_offset, per_page_func, per_gap_func]
                    (const auto p, uint64_t off) {
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
            ForEveryPageInRange(self, per_page_wrapper_fn, start_offset, end_offset);
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
};
