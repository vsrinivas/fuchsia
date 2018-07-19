// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm/vm_object_paged.h"

#include "vm_priv.h"

#include <arch/ops.h>
#include <assert.h>
#include <err.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <inttypes.h>
#include <lib/console.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <vm/fault.h>
#include <vm/physmap.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <zircon/types.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

namespace {

void ZeroPage(paddr_t pa) {
    void* ptr = paddr_to_physmap(pa);
    DEBUG_ASSERT(ptr);

    arch_zero_page(ptr);
}

void ZeroPage(vm_page_t* p) {
    paddr_t pa = p->paddr();
    ZeroPage(pa);
}

void InitializeVmPage(vm_page_t* p) {
    DEBUG_ASSERT(p->state == VM_PAGE_STATE_ALLOC);
    p->state = VM_PAGE_STATE_OBJECT;
    p->object.pin_count = 0;
}

// round up the size to the next page size boundary and make sure we dont wrap
zx_status_t RoundSize(uint64_t size, uint64_t* out_size) {
    *out_size = ROUNDUP_PAGE_SIZE(size);
    if (*out_size < size)
        return ZX_ERR_OUT_OF_RANGE;

    // there's a max size to keep indexes within range
    if (*out_size > VmObjectPaged::MAX_SIZE)
        return ZX_ERR_OUT_OF_RANGE;

    return ZX_OK;
}

} // namespace

VmObjectPaged::VmObjectPaged(
    uint32_t options, uint32_t pmm_alloc_flags, uint64_t size, fbl::RefPtr<VmObject> parent)
        : VmObject(fbl::move(parent)),
          options_(options),
          size_(size),
          pmm_alloc_flags_(pmm_alloc_flags) {
    LTRACEF("%p\n", this);

    DEBUG_ASSERT(IS_PAGE_ALIGNED(size_));
}

VmObjectPaged::~VmObjectPaged() {
    canary_.Assert();

    LTRACEF("%p\n", this);

    page_list_.ForEveryPage(
        [this](const auto p, uint64_t off) {
            if (this->is_contiguous()) {
                p->object.pin_count--;
            }
            ASSERT(p->object.pin_count == 0);
            return ZX_ERR_NEXT;
        });

    // free all of the pages attached to us
    page_list_.FreeAllPages();
}

zx_status_t VmObjectPaged::Create(uint32_t pmm_alloc_flags,
                                  uint32_t options,
                                  uint64_t size, fbl::RefPtr<VmObject>* obj) {
    // make sure size is page aligned
    zx_status_t status = RoundSize(size, &size);
    if (status != ZX_OK) {
        return status;
    }

    if (options & kContiguous) {
        // Force callers to use CreateContiguous() instead.
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AllocChecker ac;
    auto vmo = fbl::AdoptRef<VmObject>(
        new (&ac) VmObjectPaged(options, pmm_alloc_flags, size, nullptr));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    *obj = fbl::move(vmo);

    return ZX_OK;
}

zx_status_t VmObjectPaged::CreateContiguous(uint32_t pmm_alloc_flags, uint64_t size,
                                            uint8_t alignment_log2, fbl::RefPtr<VmObject>* obj) {
    DEBUG_ASSERT(alignment_log2 < sizeof(uint64_t) * 8);
    // make sure size is page aligned
    zx_status_t status = RoundSize(size, &size);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    auto vmo = fbl::AdoptRef<VmObject>(
        new (&ac) VmObjectPaged(kContiguous, pmm_alloc_flags, size, nullptr));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    if (size == 0) {
        *obj = fbl::move(vmo);
        return ZX_OK;
    }

    // allocate the pages
    list_node page_list;
    list_initialize(&page_list);

    size_t num_pages = size / PAGE_SIZE;
    size_t allocated = pmm_alloc_contiguous(num_pages, pmm_alloc_flags, alignment_log2, nullptr, &page_list);
    if (allocated != num_pages) {
        LTRACEF("failed to allocate enough pages (asked for %zu, got %zu)\n", num_pages, allocated);
        pmm_free(&page_list);
        return ZX_ERR_NO_MEMORY;
    }
    auto cleanup_phys_pages = fbl::MakeAutoCall([&page_list]() {
        pmm_free(&page_list);
    });

    DEBUG_ASSERT(list_length(&page_list) == allocated);

    // add them to the appropriate range of the object
    VmObjectPaged* vmop = static_cast<VmObjectPaged*>(vmo.get());
    for (uint64_t off = 0; off < size; off += PAGE_SIZE) {
        vm_page_t* p = list_remove_head_type(&page_list, vm_page_t, queue_node);
        ASSERT(p);

        InitializeVmPage(p);

        // TODO: remove once pmm returns zeroed pages
        ZeroPage(p);

        // We don't need thread-safety analysis here, since this VMO has not
        // been shared anywhere yet.
        [&]() TA_NO_THREAD_SAFETY_ANALYSIS {
            status = vmop->page_list_.AddPage(p, off);
        }();
        if (status != ZX_OK) {
            return status;
        }

        // Mark the pages as pinned, so they can't be physically rearranged
        // underneath us.
        p->object.pin_count++;
    }

    cleanup_phys_pages.cancel();
    *obj = fbl::move(vmo);
    return ZX_OK;
}

zx_status_t VmObjectPaged::CreateFromROData(const void* data, size_t size, fbl::RefPtr<VmObject>* obj) {
    LTRACEF("data %p, size %zu\n", data, size);

    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = Create(PMM_ALLOC_FLAG_ANY, 0, size, &vmo);
    if (status != ZX_OK)
        return status;

    if (size > 0) {
        ASSERT(IS_PAGE_ALIGNED(size));
        ASSERT(IS_PAGE_ALIGNED(reinterpret_cast<uintptr_t>(data)));

        // Do a direct lookup of the physical pages backing the range of
        // the kernel that these addresses belong to and jam them directly
        // into the VMO.
        //
        // NOTE: This relies on the kernel not otherwise owning the pages.
        // If the setup of the kernel's address space changes so that the
        // pages are attached to a kernel VMO, this will need to change.

        paddr_t start_paddr = vaddr_to_paddr(data);
        ASSERT(start_paddr != 0);

        for (size_t count = 0; count < size / PAGE_SIZE; count++) {
            paddr_t pa = start_paddr + count * PAGE_SIZE;
            vm_page_t* page = paddr_to_vm_page(pa);
            ASSERT(page);

            if (page->state == VM_PAGE_STATE_WIRED) {
                // it's wired to the kernel, so we can just use it directly
            } else if (page->state == VM_PAGE_STATE_FREE) {
                ASSERT(pmm_alloc_range(pa, 1, nullptr) == 1);
                page->state = VM_PAGE_STATE_WIRED;
            } else {
                panic("page used to back static vmo in unusable state: paddr %#" PRIxPTR " state %u\n", pa,
                      page->state);
            }

            // XXX hack to work around the ref pointer to the base class
            auto vmo2 = static_cast<VmObjectPaged*>(vmo.get());
            vmo2->AddPage(page, count * PAGE_SIZE);
        }
    }

    *obj = fbl::move(vmo);

    return ZX_OK;
}

zx_status_t VmObjectPaged::CloneCOW(bool resizable, uint64_t offset, uint64_t size,
    bool copy_name, fbl::RefPtr<VmObject>* clone_vmo) {
    LTRACEF("vmo %p offset %#" PRIx64 " size %#" PRIx64 "\n", this, offset, size);

    canary_.Assert();

    // make sure size is page aligned
    zx_status_t status = RoundSize(size, &size);
    if (status != ZX_OK)
        return status;

    auto options = resizable ? kResizable : 0u;

    // allocate the clone up front outside of our lock
    fbl::AllocChecker ac;
    auto vmo = fbl::AdoptRef<VmObjectPaged>(
        new (&ac) VmObjectPaged(options, pmm_alloc_flags_, size, fbl::WrapRefPtr(this)));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    Guard<fbl::Mutex> guard{&lock_};

    // add the new VMO as a child before we do anything, since its
    // dtor expects to find it in its parent's child list
    AddChildLocked(vmo.get());

    // check that we're not uncached in some way
    if (cache_policy_ != ARCH_MMU_FLAG_CACHED) {
        return ZX_ERR_BAD_STATE;
    }

    // set the offset with the parent
    status = vmo->SetParentOffsetLocked(offset);
    if (status != ZX_OK)
        return status;

    if (copy_name)
        vmo->name_ = name_;

    *clone_vmo = fbl::move(vmo);

    return ZX_OK;
}

void VmObjectPaged::Dump(uint depth, bool verbose) {
    canary_.Assert();

    // This can grab our lock.
    uint64_t parent_id = parent_user_id();

    Guard<fbl::Mutex> guard{&lock_};

    size_t count = 0;
    page_list_.ForEveryPage([&count](const auto p, uint64_t) {
        count++;
        return ZX_ERR_NEXT;
    });

    for (uint i = 0; i < depth; ++i) {
        printf("  ");
    }
    printf("vmo %p/k%" PRIu64 " size %#" PRIx64
           " pages %zu ref %d parent k%" PRIu64 "\n",
           this, user_id_, size_, count, ref_count_debug(), parent_id);

    if (verbose) {
        auto f = [depth](const auto p, uint64_t offset) {
            for (uint i = 0; i < depth + 1; ++i) {
                printf("  ");
            }
            printf("offset %#" PRIx64 " page %p paddr %#" PRIxPTR "\n", offset, p, p->paddr());
            return ZX_ERR_NEXT;
        };
        page_list_.ForEveryPage(f);
    }
}

size_t VmObjectPaged::AllocatedPagesInRange(uint64_t offset, uint64_t len) const {
    canary_.Assert();
    Guard<fbl::Mutex> guard{&lock_};
    uint64_t new_len;
    if (!TrimRange(offset, len, size_, &new_len)) {
        return 0;
    }
    size_t count = 0;
    // TODO: Figure out what to do with our parent's pages. If we're a clone,
    // page_list_ only contains pages that we've made copies of.
    page_list_.ForEveryPage(
        [&count, offset, new_len](const auto p, uint64_t off) {
            if (off >= offset && off < offset + new_len) {
                count++;
            }
            return ZX_ERR_NEXT;
        });
    return count;
}

zx_status_t VmObjectPaged::AddPage(vm_page_t* p, uint64_t offset) {
    Guard<fbl::Mutex> guard{&lock_};

    return AddPageLocked(p, offset);
}

zx_status_t VmObjectPaged::AddPageLocked(vm_page_t* p, uint64_t offset) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.lock().IsHeld());

    LTRACEF("vmo %p, offset %#" PRIx64 ", page %p (%#" PRIxPTR ")\n", this, offset, p, p->paddr());

    DEBUG_ASSERT(p);

    if (offset >= size_)
        return ZX_ERR_OUT_OF_RANGE;

    zx_status_t err = page_list_.AddPage(p, offset);
    if (err != ZX_OK)
        return err;

    // other mappings may have covered this offset into the vmo, so unmap those ranges
    RangeChangeUpdateLocked(offset, PAGE_SIZE);

    return ZX_OK;
}

// Looks up the page at the requested offset, faulting it in if requested and necessary.  If
// this VMO has a parent and the requested page isn't found, the parent will be searched.
//
// |free_list|, if not NULL, is a list of allocated but unused vm_page_t that
// this function may allocate from.  This function will need at most one entry,
// and will not fail if |free_list| is a non-empty list, faulting in was requested,
// and offset is in range.
zx_status_t VmObjectPaged::GetPageLocked(uint64_t offset, uint pf_flags, list_node* free_list,
                                         vm_page_t** const page_out, paddr_t* const pa_out) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.lock().IsHeld());

    if (offset >= size_)
        return ZX_ERR_OUT_OF_RANGE;

    vm_page_t* p;
    paddr_t pa;

    // see if we already have a page at that offset
    p = page_list_.GetPage(offset);
    if (p) {
        if (page_out)
            *page_out = p;
        if (pa_out)
            *pa_out = p->paddr();
        return ZX_OK;
    }

    __UNUSED char pf_string[5];
    LTRACEF("vmo %p, offset %#" PRIx64 ", pf_flags %#x (%s)\n", this, offset, pf_flags,
            vmm_pf_flags_to_string(pf_flags, pf_string));

    // if we have a parent see if they have a page for us
    if (parent_) {
        uint64_t parent_offset;
        bool overflowed = add_overflow(parent_offset_, offset, &parent_offset);
        ASSERT(!overflowed);

        // make sure we don't cause the parent to fault in new pages, just ask for any that already exist
        uint parent_pf_flags = pf_flags & ~(VMM_PF_FLAG_FAULT_MASK);

        zx_status_t status = parent_->GetPageLocked(parent_offset, parent_pf_flags,
                                                    nullptr, &p, &pa);
        if (status == ZX_OK) {
            // we have a page from them. if we're read-only faulting, return that page so they can map
            // or read from it directly
            if ((pf_flags & VMM_PF_FLAG_WRITE) == 0) {
                if (page_out)
                    *page_out = p;
                if (pa_out)
                    *pa_out = pa;

                LTRACEF("read only faulting in page %p, pa %#" PRIxPTR " from parent\n", p, pa);

                return ZX_OK;
            }

            // if we're write faulting, we need to clone it and return the new page
            paddr_t pa_clone;
            vm_page_t* p_clone = nullptr;
            if (free_list) {
                p_clone = list_remove_head_type(free_list, vm_page, queue_node);
                if (p_clone) {
                    pa_clone = p_clone->paddr();
                }
            }
            if (!p_clone) {
                p_clone = pmm_alloc_page(pmm_alloc_flags_, &pa_clone);
            }
            if (!p_clone) {
                return ZX_ERR_NO_MEMORY;
            }

            InitializeVmPage(p_clone);

            // do a direct copy of the two pages
            const void* src = paddr_to_physmap(pa);
            void* dst = paddr_to_physmap(pa_clone);

            DEBUG_ASSERT(src && dst);

            memcpy(dst, src, PAGE_SIZE);

            // add the new page and return it
            status = AddPageLocked(p_clone, offset);
            DEBUG_ASSERT(status == ZX_OK);

            LTRACEF("copy-on-write faulted in page %p, pa %#" PRIxPTR " copied from %p, pa %#" PRIxPTR "\n",
                    p, pa, p_clone, pa_clone);

            if (page_out)
                *page_out = p_clone;
            if (pa_out)
                *pa_out = pa_clone;

            return ZX_OK;
        }
    }

    // if we're not being asked to sw or hw fault in the page, return not found
    if ((pf_flags & VMM_PF_FLAG_FAULT_MASK) == 0)
        return ZX_ERR_NOT_FOUND;

    // if we're read faulting, we don't already have a page, and the parent doesn't have it,
    // return the single global zero page
    if ((pf_flags & VMM_PF_FLAG_WRITE) == 0) {
        LTRACEF("returning the zero page\n");
        if (page_out)
            *page_out = vm_get_zero_page();
        if (pa_out)
            *pa_out = vm_get_zero_page_paddr();
        return ZX_OK;
    }

    // allocate a page
    if (free_list) {
        p = list_remove_head_type(free_list, vm_page, queue_node);
        if (p) {
            pa = p->paddr();
        }
    }
    if (!p) {
        p = pmm_alloc_page(pmm_alloc_flags_, &pa);
    }
    if (!p) {
        return ZX_ERR_NO_MEMORY;
    }

    InitializeVmPage(p);

    // TODO: remove once pmm returns zeroed pages
    ZeroPage(pa);

// if ARM and not fully cached, clean/invalidate the page after zeroing it
#if ARCH_ARM64
    if (cache_policy_ != ARCH_MMU_FLAG_CACHED) {
        arch_clean_invalidate_cache_range((addr_t)paddr_to_physmap(pa), PAGE_SIZE);
    }
#endif

    zx_status_t status = AddPageLocked(p, offset);
    DEBUG_ASSERT(status == ZX_OK);

    // other mappings may have covered this offset into the vmo, so unmap those ranges
    RangeChangeUpdateLocked(offset, PAGE_SIZE);

    LTRACEF("faulted in page %p, pa %#" PRIxPTR "\n", p, pa);

    if (page_out)
        *page_out = p;
    if (pa_out)
        *pa_out = pa;

    return ZX_OK;
}

zx_status_t VmObjectPaged::CommitRange(uint64_t offset, uint64_t len, uint64_t* committed) {
    canary_.Assert();
    LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 "\n", offset, len);

    if (committed)
        *committed = 0;

    Guard<fbl::Mutex> guard{&lock_};

    // trim the size
    uint64_t new_len;
    if (!TrimRange(offset, len, size_, &new_len))
        return ZX_ERR_OUT_OF_RANGE;

    // was in range, just zero length
    if (new_len == 0)
        return ZX_OK;

    // compute a page aligned end to do our searches in to make sure we cover all the pages
    uint64_t end = ROUNDUP_PAGE_SIZE(offset + new_len);
    DEBUG_ASSERT(end > offset);
    offset = ROUNDDOWN(offset, PAGE_SIZE);

    // make a pass through the list, counting the number of pages we need to allocate
    size_t count = 0;
    uint64_t expected_next_off = offset;
    page_list_.ForEveryPageInRange(
        [&count, &expected_next_off](const auto p, uint64_t off) {

            count += (off - expected_next_off) / PAGE_SIZE;
            expected_next_off = off + PAGE_SIZE;
            return ZX_ERR_NEXT;
        },
        expected_next_off, end);

    // If expected_next_off isn't at the end of the range, there was a gap at
    // the end.  Add it back in
    DEBUG_ASSERT(end >= expected_next_off);
    count += (end - expected_next_off) / PAGE_SIZE;
    if (count == 0)
        return ZX_OK;

    // allocate count number of pages
    list_node page_list;
    list_initialize(&page_list);

    size_t allocated = pmm_alloc_pages(count, pmm_alloc_flags_, &page_list);
    if (allocated < count) {
        LTRACEF("failed to allocate enough pages (asked for %zu, got %zu)\n", count, allocated);
        pmm_free(&page_list);
        return ZX_ERR_NO_MEMORY;
    }

    // unmap all of the pages in this range on all the mapping regions
    RangeChangeUpdateLocked(offset, end - offset);

    // add them to the appropriate range of the object
    for (uint64_t o = offset; o < end; o += PAGE_SIZE) {
        // Don't commit if we already have this page
        vm_page_t* p = page_list_.GetPage(o);
        if (p) {
            continue;
        }

        // Check if our parent has the page
        paddr_t pa;
        const uint flags = VMM_PF_FLAG_SW_FAULT | VMM_PF_FLAG_WRITE;
        // Should not be able to fail, since we're providing it memory and the
        // range should be valid.
        zx_status_t status = GetPageLocked(o, flags, &page_list, &p, &pa);
        ASSERT(status == ZX_OK);

        if (committed)
            *committed += PAGE_SIZE;
    }

    DEBUG_ASSERT(list_is_empty(&page_list));

    // for now we only support committing as much as we were asked for
    DEBUG_ASSERT(!committed || *committed == count * PAGE_SIZE);

    return ZX_OK;
}

zx_status_t VmObjectPaged::DecommitRange(uint64_t offset, uint64_t len, uint64_t* decommitted) {
    canary_.Assert();
    LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 "\n", offset, len);

    if (decommitted)
        *decommitted = 0;

    if (options_ & kContiguous) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    Guard<fbl::Mutex> guard{&lock_};

    // trim the size
    uint64_t new_len;
    if (!TrimRange(offset, len, size_, &new_len))
        return ZX_ERR_OUT_OF_RANGE;

    // was in range, just zero length
    if (new_len == 0)
        return ZX_OK;

    // figure the starting and ending page offset
    uint64_t start = ROUNDDOWN(offset, PAGE_SIZE);
    uint64_t end = ROUNDUP_PAGE_SIZE(offset + new_len);
    DEBUG_ASSERT(end > offset);
    DEBUG_ASSERT(end > start);
    uint64_t page_aligned_len = end - start;

    LTRACEF("start offset %#" PRIx64 ", end %#" PRIx64 ", page_aliged_len %#" PRIx64 "\n", start, end,
            page_aligned_len);

    // TODO(teisenbe): Allow decommitting of pages pinned by
    // CommitRangeContiguous

    if (AnyPagesPinnedLocked(start, page_aligned_len)) {
        return ZX_ERR_BAD_STATE;
    }

    // unmap all of the pages in this range on all the mapping regions
    RangeChangeUpdateLocked(start, page_aligned_len);

    // iterate through the pages, freeing them
    // TODO: use page_list iterator, move pages to list, free at once
    while (start < end) {
        auto status = page_list_.FreePage(start);
        if (status == ZX_OK && decommitted) {
            *decommitted += PAGE_SIZE;
        }
        start += PAGE_SIZE;
    }

    return ZX_OK;
}

zx_status_t VmObjectPaged::Pin(uint64_t offset, uint64_t len) {
    canary_.Assert();

    Guard<fbl::Mutex> guard{&lock_};
    return PinLocked(offset, len);
}

zx_status_t VmObjectPaged::PinLocked(uint64_t offset, uint64_t len) {
    canary_.Assert();

    // verify that the range is within the object
    if (unlikely(!InRange(offset, len, size_)))
        return ZX_ERR_OUT_OF_RANGE;

    if (unlikely(len == 0))
        return ZX_OK;

    const uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
    const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

    uint64_t expected_next_off = start_page_offset;
    zx_status_t status = page_list_.ForEveryPageInRange(
        [&expected_next_off](const auto p, uint64_t off) {
            if (off != expected_next_off) {
                return ZX_ERR_NOT_FOUND;
            }

            DEBUG_ASSERT(p->state == VM_PAGE_STATE_OBJECT);
            if (p->object.pin_count == VM_PAGE_OBJECT_MAX_PIN_COUNT) {
                return ZX_ERR_UNAVAILABLE;
            }

            p->object.pin_count++;
            expected_next_off = off + PAGE_SIZE;
            return ZX_ERR_NEXT;
        },
        start_page_offset, end_page_offset);

    if (status == ZX_OK && expected_next_off != end_page_offset) {
        status = ZX_ERR_NOT_FOUND;
    }
    if (status != ZX_OK) {
        UnpinLocked(start_page_offset, expected_next_off - start_page_offset);
        return status;
    }

    return ZX_OK;
}

void VmObjectPaged::Unpin(uint64_t offset, uint64_t len) {
    Guard<fbl::Mutex> guard{&lock_};
    UnpinLocked(offset, len);
}

void VmObjectPaged::UnpinLocked(uint64_t offset, uint64_t len) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.lock().IsHeld());

    // verify that the range is within the object
    ASSERT(InRange(offset, len, size_));

    if (unlikely(len == 0))
        return;

    const uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
    const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

    uint64_t expected_next_off = start_page_offset;
    zx_status_t status = page_list_.ForEveryPageInRange(
        [&expected_next_off](const auto p, uint64_t off) {
            if (off != expected_next_off) {
                return ZX_ERR_NOT_FOUND;
            }

            DEBUG_ASSERT(p->state == VM_PAGE_STATE_OBJECT);
            ASSERT(p->object.pin_count > 0);
            p->object.pin_count--;
            expected_next_off = off + PAGE_SIZE;
            return ZX_ERR_NEXT;
        },
        start_page_offset, end_page_offset);
    ASSERT_MSG(status == ZX_OK && expected_next_off == end_page_offset,
               "Tried to unpin an uncommitted page");
    return;
}

bool VmObjectPaged::AnyPagesPinnedLocked(uint64_t offset, size_t len) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.lock().IsHeld());
    DEBUG_ASSERT(IS_PAGE_ALIGNED(offset));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(len));

    const uint64_t start_page_offset = offset;
    const uint64_t end_page_offset = offset + len;

    bool found_pinned = false;
    page_list_.ForEveryPageInRange(
        [&found_pinned, start_page_offset, end_page_offset](const auto p, uint64_t off) {
            DEBUG_ASSERT(off >= start_page_offset && off < end_page_offset);
            if (p->object.pin_count > 0) {
                found_pinned = true;
                return ZX_ERR_STOP;
            }
            return ZX_ERR_NEXT;
        },
        start_page_offset, end_page_offset);

    return found_pinned;
}

zx_status_t VmObjectPaged::ResizeLocked(uint64_t s) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.lock().IsHeld());

    LTRACEF("vmo %p, size %" PRIu64 "\n", this, s);

    if (!(options_ & kResizable)) {
        return ZX_ERR_UNAVAILABLE;
    }

    // round up the size to the next page size boundary and make sure we dont wrap
    zx_status_t status = RoundSize(s, &s);
    if (status != ZX_OK)
        return status;

    // make sure everything is aligned before we get started
    DEBUG_ASSERT(IS_PAGE_ALIGNED(size_));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(s));

    // see if we're shrinking or expanding the vmo
    if (s < size_) {
        // shrinking
        uint64_t start = s;
        uint64_t end = size_;
        uint64_t len = end - start;

        // bail if there are any pinned pages in the range we're trimming
        if (AnyPagesPinnedLocked(start, len)) {
            return ZX_ERR_BAD_STATE;
        }

        // unmap all of the pages in this range on all the mapping regions
        RangeChangeUpdateLocked(start, len);

        // iterate through the pages, freeing them
        // TODO: use page_list iterator, move pages to list, free at once
        while (start < end) {
            page_list_.FreePage(start);
            start += PAGE_SIZE;
        }
    } else if (s > size_) {
        // expanding
        // figure the starting and ending page offset that is affected
        uint64_t start = size_;
        uint64_t end = s;
        uint64_t len = end - start;

        // inform all our children or mapping that there's new bits
        RangeChangeUpdateLocked(start, len);
    }

    // save bytewise size
    size_ = s;

    return ZX_OK;
}

zx_status_t VmObjectPaged::Resize(uint64_t s) {
    Guard<fbl::Mutex> guard{&lock_};

    return ResizeLocked(s);
}

zx_status_t VmObjectPaged::SetParentOffsetLocked(uint64_t offset) {
    DEBUG_ASSERT(lock_.lock().IsHeld());

    // offset must be page aligned
    if (!IS_PAGE_ALIGNED(offset))
        return ZX_ERR_INVALID_ARGS;

    // TODO: ZX-692 make sure that the accumulated offset of the entire parent chain doesn't wrap 64bit space

    // make sure the size + this offset are still valid
    uint64_t end;
    if (add_overflow(offset, size_, &end)) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    parent_offset_ = offset;

    return ZX_OK;
}

// perform some sort of copy in/out on a range of the object using a passed in lambda
// for the copy routine
template <typename T>
zx_status_t VmObjectPaged::ReadWriteInternal(uint64_t offset, size_t len, bool write, T copyfunc) {
    canary_.Assert();

    Guard<fbl::Mutex> guard{&lock_};

    // are we uncached? abort in this case
    if (cache_policy_ != ARCH_MMU_FLAG_CACHED)
        return ZX_ERR_BAD_STATE;

    // test if in range
    uint64_t end_offset;
    if (add_overflow(offset, len, &end_offset) || end_offset > size_) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    // walk the list of pages and do the write
    uint64_t src_offset = offset;
    size_t dest_offset = 0;
    while (len > 0) {
        size_t page_offset = src_offset % PAGE_SIZE;
        size_t tocopy = MIN(PAGE_SIZE - page_offset, len);

        // fault in the page
        paddr_t pa;
        auto status = GetPageLocked(src_offset,
                                    VMM_PF_FLAG_SW_FAULT | (write ? VMM_PF_FLAG_WRITE : 0),
                                    nullptr, nullptr, &pa);
        if (status < 0)
            return status;

        // compute the kernel mapping of this page
        uint8_t* page_ptr = reinterpret_cast<uint8_t*>(paddr_to_physmap(pa));

        // call the copy routine
        auto err = copyfunc(page_ptr + page_offset, dest_offset, tocopy);
        if (err < 0)
            return err;

        src_offset += tocopy;
        dest_offset += tocopy;
        len -= tocopy;
    }

    return ZX_OK;
}

zx_status_t VmObjectPaged::Read(void* _ptr, uint64_t offset, size_t len) {
    canary_.Assert();
    // test to make sure this is a kernel pointer
    if (!is_kernel_address(reinterpret_cast<vaddr_t>(_ptr))) {
        DEBUG_ASSERT_MSG(0, "non kernel pointer passed\n");
        return ZX_ERR_INVALID_ARGS;
    }

    // read routine that just uses a memcpy
    uint8_t* ptr = reinterpret_cast<uint8_t*>(_ptr);
    auto read_routine = [ptr](const void* src, size_t offset, size_t len) -> zx_status_t {
        memcpy(ptr + offset, src, len);
        return ZX_OK;
    };

    return ReadWriteInternal(offset, len, false, read_routine);
}

zx_status_t VmObjectPaged::Write(const void* _ptr, uint64_t offset, size_t len) {
    canary_.Assert();
    // test to make sure this is a kernel pointer
    if (!is_kernel_address(reinterpret_cast<vaddr_t>(_ptr))) {
        DEBUG_ASSERT_MSG(0, "non kernel pointer passed\n");
        return ZX_ERR_INVALID_ARGS;
    }

    // write routine that just uses a memcpy
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(_ptr);
    auto write_routine = [ptr](void* dst, size_t offset, size_t len) -> zx_status_t {
        memcpy(dst, ptr + offset, len);
        return ZX_OK;
    };

    return ReadWriteInternal(offset, len, true, write_routine);
}

zx_status_t VmObjectPaged::Lookup(uint64_t offset, uint64_t len, uint pf_flags,
                                  vmo_lookup_fn_t lookup_fn, void* context) {
    canary_.Assert();
    if (unlikely(len == 0))
        return ZX_ERR_INVALID_ARGS;

    Guard<fbl::Mutex> guard{&lock_};

    // verify that the range is within the object
    if (unlikely(!InRange(offset, len, size_)))
        return ZX_ERR_OUT_OF_RANGE;

    const uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
    const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

    uint64_t expected_next_off = start_page_offset;
    zx_status_t status = page_list_.ForEveryPageInRange(
        [&expected_next_off, this, pf_flags, lookup_fn, context,
         start_page_offset](const auto p, uint64_t off) {

            // If some page was missing from our list, run the more expensive
            // GetPageLocked to see if our parent has it.
            for (uint64_t missing_off = expected_next_off; missing_off < off;
                 missing_off += PAGE_SIZE) {

                paddr_t pa;
                zx_status_t status = this->GetPageLocked(missing_off, pf_flags, nullptr,
                                                         nullptr, &pa);
                if (status != ZX_OK) {
                    return ZX_ERR_NO_MEMORY;
                }
                const size_t index = (off - start_page_offset) / PAGE_SIZE;
                status = lookup_fn(context, missing_off, index, pa);
                if (status != ZX_OK) {
                    if (unlikely(status == ZX_ERR_NEXT || status == ZX_ERR_STOP)) {
                        status = ZX_ERR_INTERNAL;
                    }
                    return status;
                }
            }

            const size_t index = (off - start_page_offset) / PAGE_SIZE;
            paddr_t pa = p->paddr();
            zx_status_t status = lookup_fn(context, off, index, pa);
            if (status != ZX_OK) {
                if (unlikely(status == ZX_ERR_NEXT || status == ZX_ERR_STOP)) {
                    status = ZX_ERR_INTERNAL;
                }
                return status;
            }

            expected_next_off = off + PAGE_SIZE;
            return ZX_ERR_NEXT;
        },
        start_page_offset, end_page_offset);
    if (status != ZX_OK) {
        return status;
    }

    // If expected_next_off isn't at the end, there's a gap to process
    for (uint64_t off = expected_next_off; off < end_page_offset; off += PAGE_SIZE) {
        paddr_t pa;
        zx_status_t status = GetPageLocked(off, pf_flags, nullptr, nullptr, &pa);
        if (status != ZX_OK) {
            return ZX_ERR_NO_MEMORY;
        }
        const size_t index = (off - start_page_offset) / PAGE_SIZE;
        status = lookup_fn(context, off, index, pa);
        if (status != ZX_OK) {
            return status;
        }
    }

    return ZX_OK;
}

zx_status_t VmObjectPaged::ReadUser(user_out_ptr<void> ptr, uint64_t offset, size_t len) {
    canary_.Assert();

    // read routine that uses copy_to_user
    auto read_routine = [ptr](const void* src, size_t offset, size_t len) -> zx_status_t {
        return ptr.byte_offset(offset).copy_array_to_user(src, len);
    };

    return ReadWriteInternal(offset, len, false, read_routine);
}

zx_status_t VmObjectPaged::WriteUser(user_in_ptr<const void> ptr, uint64_t offset, size_t len) {
    canary_.Assert();

    // write routine that uses copy_from_user
    auto write_routine = [ptr](void* dst, size_t offset, size_t len) -> zx_status_t {
        return ptr.byte_offset(offset).copy_array_from_user(dst, len);
    };

    return ReadWriteInternal(offset, len, true, write_routine);
}

zx_status_t VmObjectPaged::LookupUser(uint64_t offset, uint64_t len, user_inout_ptr<paddr_t> buffer,
                                      size_t buffer_size) {
    canary_.Assert();

    uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
    uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);
    // compute the size of the table we'll need and make sure it fits in the user buffer
    uint64_t table_size = ((end_page_offset - start_page_offset) / PAGE_SIZE) * sizeof(paddr_t);
    if (unlikely(table_size > buffer_size))
        return ZX_ERR_BUFFER_TOO_SMALL;

    auto copy_to_user = [](void* context, size_t offset, size_t index, paddr_t pa) -> zx_status_t {
        user_inout_ptr<paddr_t>* buffer = static_cast<user_inout_ptr<paddr_t>*>(context);
        return buffer->element_offset(index).copy_to_user(pa);
    };
    // only lookup pages that are already present
    return Lookup(offset, len, 0, copy_to_user, &buffer);
}

zx_status_t VmObjectPaged::InvalidateCache(const uint64_t offset, const uint64_t len) {
    return CacheOp(offset, len, CacheOpType::Invalidate);
}

zx_status_t VmObjectPaged::CleanCache(const uint64_t offset, const uint64_t len) {
    return CacheOp(offset, len, CacheOpType::Clean);
}

zx_status_t VmObjectPaged::CleanInvalidateCache(const uint64_t offset, const uint64_t len) {
    return CacheOp(offset, len, CacheOpType::CleanInvalidate);
}

zx_status_t VmObjectPaged::SyncCache(const uint64_t offset, const uint64_t len) {
    return CacheOp(offset, len, CacheOpType::Sync);
}

zx_status_t VmObjectPaged::CacheOp(const uint64_t start_offset, const uint64_t len,
                                   const CacheOpType type) {
    canary_.Assert();

    if (unlikely(len == 0))
        return ZX_ERR_INVALID_ARGS;

    Guard<fbl::Mutex> guard{&lock_};

    if (unlikely(!InRange(start_offset, len, size_)))
        return ZX_ERR_OUT_OF_RANGE;

    const size_t end_offset = static_cast<size_t>(start_offset + len);
    size_t op_start_offset = static_cast<size_t>(start_offset);

    while (op_start_offset != end_offset) {
        // Offset at the end of the current page.
        const size_t page_end_offset = ROUNDUP(op_start_offset + 1, PAGE_SIZE);

        // This cache op will either terminate at the end of the current page or
        // at the end of the whole op range -- whichever comes first.
        const size_t op_end_offset = MIN(page_end_offset, end_offset);

        const size_t cache_op_len = op_end_offset - op_start_offset;

        const size_t page_offset = op_start_offset % PAGE_SIZE;

        // lookup the physical address of the page, careful not to fault in a new one
        paddr_t pa;
        auto status = GetPageLocked(op_start_offset, 0, nullptr, nullptr, &pa);

        if (likely(status == ZX_OK)) {
            // Convert the page address to a Kernel virtual address.
            const void* ptr = paddr_to_physmap(pa);
            const addr_t cache_op_addr = reinterpret_cast<addr_t>(ptr) + page_offset;

            LTRACEF("ptr %p op %d\n", ptr, (int)type);

            // Perform the necessary cache op against this page.
            switch (type) {
            case CacheOpType::Invalidate:
                arch_invalidate_cache_range(cache_op_addr, cache_op_len);
                break;
            case CacheOpType::Clean:
                arch_clean_cache_range(cache_op_addr, cache_op_len);
                break;
            case CacheOpType::CleanInvalidate:
                arch_clean_invalidate_cache_range(cache_op_addr, cache_op_len);
                break;
            case CacheOpType::Sync:
                arch_sync_cache_range(cache_op_addr, cache_op_len);
                break;
            }
        }

        op_start_offset += cache_op_len;
    }

    return ZX_OK;
}

zx_status_t VmObjectPaged::GetMappingCachePolicy(uint32_t* cache_policy) {
    Guard<fbl::Mutex> guard{&lock_};

    *cache_policy = cache_policy_;

    return ZX_OK;
}

zx_status_t VmObjectPaged::SetMappingCachePolicy(const uint32_t cache_policy) {
    // Is it a valid cache flag?
    if (cache_policy & ~ZX_CACHE_POLICY_MASK) {
        return ZX_ERR_INVALID_ARGS;
    }

    Guard<fbl::Mutex> guard{&lock_};

    // conditions for allowing the cache policy to be set:
    // 1) vmo has no pages committed currently
    // 2) vmo has no mappings
    // 3) vmo has no clones
    // 4) vmo is not a clone
    if (!page_list_.IsEmpty()) {
        return ZX_ERR_BAD_STATE;
    }
    if (!mapping_list_.is_empty()) {
        return ZX_ERR_BAD_STATE;
    }
    if (!children_list_.is_empty()) {
        return ZX_ERR_BAD_STATE;
    }
    if (parent_) {
        return ZX_ERR_BAD_STATE;
    }

    cache_policy_ = cache_policy;

    return ZX_OK;
}

void VmObjectPaged::RangeChangeUpdateFromParentLocked(const uint64_t offset, const uint64_t len) {
    canary_.Assert();

    LTRACEF("offset %#" PRIx64 " len %#" PRIx64 " p_offset %#" PRIx64 " size_ %#" PRIx64 "\n",
            offset, len, parent_offset_, size_);

    // our parent is notifying that a range of theirs changed, see where it intersects
    // with our offset into the parent and pass it on
    uint64_t offset_new;
    uint64_t len_new;
    if (!GetIntersect(parent_offset_, size_, offset, len,
                      &offset_new, &len_new))
        return;

    // if they intersect with us, then by definition the new offset must be >= parent_offset_
    DEBUG_ASSERT(offset_new >= parent_offset_);

    // subtract our offset
    offset_new -= parent_offset_;

    // verify that it's still within range of us
    DEBUG_ASSERT(offset_new + len_new <= size_);

    LTRACEF("new offset %#" PRIx64 " new len %#" PRIx64 "\n",
            offset_new, len_new);

    // pass it on
    // TODO: optimize by not passing on ranges that are completely covered by pages local to this vmo
    RangeChangeUpdateLocked(offset_new, len_new);
}
