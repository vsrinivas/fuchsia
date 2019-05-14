// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <fbl/canary.h>
#include <fbl/function.h>
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

    void set_offset(uint64_t offset) {
        DEBUG_ASSERT(!InContainer());
        obj_offset_ = offset;
    }

    // for every valid page in the node call the passed in function
    template <typename F>
    zx_status_t ForEveryPage(F func, uint64_t start_offset, uint64_t end_offset, uint64_t skew) {
        return ForEveryPage(this, func, start_offset, end_offset, skew);
    }

    // for every valid page in the node call the passed in function
    template <typename F>
    zx_status_t ForEveryPage(F func,
                             uint64_t start_offset, uint64_t end_offset, uint64_t skew) const {
        return ForEveryPage(this, func, start_offset, end_offset, skew);
    }

    vm_page* GetPage(size_t index) const;
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
    static zx_status_t ForEveryPage(S self, F func,
                                    uint64_t start_offset, uint64_t end_offset, uint64_t skew) {
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
                zx_status_t status = func(self->pages_[i],
                                          self->obj_offset_ + i * PAGE_SIZE - skew);
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

    void InitializeSkew(uint64_t parent_skew, uint64_t offset) {
        // Checking list_skew_ doesn't catch all instances of double-initialization, but
        // it should catch some of them.
        DEBUG_ASSERT(list_skew_ == 0);
        DEBUG_ASSERT(list_.is_empty());

        list_skew_ = (parent_skew + offset) % (PAGE_SIZE * VmPageListNode::kPageFanOut);
    }
    uint64_t GetSkew() const {
        return list_skew_;
    }

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
    vm_page* GetPage(uint64_t offset) const;
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
    // Invokes T on each page in [start_offset, end_offset) and for any pages for
    // which it returns true, puts them on |removed_pages|. The caller now owns
    // the pages.
    template <typename T>
    void RemovePages(T per_page_fn, uint64_t start_offset, uint64_t end_offset,
                     list_node_t* removed_pages) {
        DEBUG_ASSERT(removed_pages);

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
        auto per_page_func = [per_page_fn, &removed_pages](auto*& p, uint64_t offset) {
            if (per_page_fn(p, offset)) {
                list_add_tail(removed_pages, &p->queue_node);
                p = nullptr;
            }
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
    bool IsEmpty();

    // Merges the pages in |other| in the range [|offset|, |end_offset|) into |this|
    // page list, starting at offset 0 in this list.
    //
    // For every page in |other| in the given range, if there is no corresponding page
    // in |this|, then they will be passed to |migrate_fn| and migrated into |this|. For
    // any pages in |other| outside the given range or which conflict with a page in |this|,
    // they will be passed to |release_fn| and then added to |free_list|.
    //
    // The |offset| values passed to |release_fn| and |migrate_fn| are the original offsets
    // in |other|, not the adapted offsets in |this|.
    void MergeFrom(VmPageList& other, uint64_t offset, uint64_t end_offset,
                   fbl::Function<void(vm_page*, uint64_t offset)> release_fn,
                   fbl::Function<void(vm_page*, uint64_t offset)> migrate_fn,
                   list_node_t* free_list);

    // Takes the pages in the range [offset, length) out of this page list.
    VmPageSpliceList TakePages(uint64_t offset, uint64_t length);

    // Allow the implementation to use a one-past-the-end for VmPageListNode offsets,
    // plus to account for skew_.
    static constexpr uint64_t MAX_SIZE =
            ROUNDDOWN(UINT64_MAX, 2 * VmPageListNode::kPageFanOut * PAGE_SIZE);

private:
    template <typename S, typename F>
    static zx_status_t ForEveryPage(S self, F per_page_func) {
        for (auto& pl : self->list_) {
            zx_status_t status = pl.ForEveryPage(per_page_func, pl.offset(),
                                                 pl.offset() + pl.kPageFanOut * PAGE_SIZE,
                                                 self->list_skew_);
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
            zx_status_t status = pl.ForEveryPage(per_page_func, start_offset,
                                                 end_offset, self->list_skew_);
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
    // A skew added to offsets provided as arguments to VmPageList functions before
    // interfacing with list_. This allows all VmPageLists within a clone tree
    // to place individual vm_page_t entries at the same offsets within their nodes, so
    // that the nodes can be moved between different lists without having to worry
    // about needing to split up a node.
    uint64_t list_skew_ = 0;
};
