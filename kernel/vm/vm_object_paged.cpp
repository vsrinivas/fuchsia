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
#include <inttypes.h>
#include <kernel/vm.h>
#include <lib/console.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <safeint/safe_math.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <vm/fault.h>
#include <vm/vm_address_region.h>

using fbl::AutoLock;

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

namespace {

void ZeroPage(paddr_t pa) {
    void* ptr = paddr_to_kvaddr(pa);
    DEBUG_ASSERT(ptr);

    arch_zero_page(ptr);
}

void ZeroPage(vm_page_t* p) {
    paddr_t pa = vm_page_to_paddr(p);
    ZeroPage(pa);
}

void InitializeVmPage(vm_page_t* p) {
    DEBUG_ASSERT(p->state == VM_PAGE_STATE_ALLOC);
    p->state = VM_PAGE_STATE_OBJECT;
    p->object.pin_count = 0;
    p->object.contiguous_pin = 0;
}

} // namespace

VmObjectPaged::VmObjectPaged(uint32_t pmm_alloc_flags, fbl::RefPtr<VmObject> parent)
    : VmObject(fbl::move(parent)), pmm_alloc_flags_(pmm_alloc_flags) {
    LTRACEF("%p\n", this);
}

VmObjectPaged::~VmObjectPaged() {
    canary_.Assert();

    LTRACEF("%p\n", this);

    page_list_.ForEveryPage(
        [](const auto p, uint64_t off) {
            if (p->object.contiguous_pin) {
                p->object.pin_count--;
            }
            ASSERT(p->object.pin_count == 0);
            return MX_ERR_NEXT;
        });

    // free all of the pages attached to us
    page_list_.FreeAllPages();
}

mx_status_t VmObjectPaged::Create(uint32_t pmm_alloc_flags, uint64_t size, fbl::RefPtr<VmObject>* obj) {
    // there's a max size to keep indexes within range
    if (size > MAX_SIZE)
        return MX_ERR_INVALID_ARGS;

    fbl::AllocChecker ac;
    auto vmo = fbl::AdoptRef<VmObject>(new (&ac) VmObjectPaged(pmm_alloc_flags, nullptr));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    auto err = vmo->Resize(size);
    if (err != MX_OK)
        return err;

    *obj = fbl::move(vmo);

    return MX_OK;
}

status_t VmObjectPaged::CloneCOW(uint64_t offset, uint64_t size, bool copy_name, fbl::RefPtr<VmObject>* clone_vmo) {
    LTRACEF("vmo %p offset %#" PRIx64 " size %#" PRIx64 "\n", this, offset, size);

    canary_.Assert();

    fbl::AllocChecker ac;
    auto vmo = fbl::AdoptRef<VmObjectPaged>(new (&ac) VmObjectPaged(pmm_alloc_flags_, fbl::WrapRefPtr(this)));
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    AutoLock a(&lock_);

    // add it as a child to us
    AddChildLocked(vmo.get());

    // set the new clone's size
    auto status = vmo->ResizeLocked(size);
    if (status != MX_OK)
        return status;

    // set the offset with the parent
    status = vmo->SetParentOffsetLocked(offset);
    if (status != MX_OK)
        return status;

    if (copy_name)
        vmo->name_ = name_;

    *clone_vmo = fbl::move(vmo);

    return MX_OK;
}

void VmObjectPaged::Dump(uint depth, bool verbose) {
    canary_.Assert();

    // This can grab our lock.
    uint64_t parent_id = parent_user_id();

    AutoLock a(&lock_);

    size_t count = 0;
    page_list_.ForEveryPage([&count](const auto p, uint64_t) {
        count++;
        return MX_ERR_NEXT;
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
            printf("offset %#" PRIx64 " page %p paddr %#" PRIxPTR "\n", offset, p, vm_page_to_paddr(p));
            return MX_ERR_NEXT;
        };
        page_list_.ForEveryPage(f);
    }
}

size_t VmObjectPaged::AllocatedPagesInRange(uint64_t offset, uint64_t len) const {
    canary_.Assert();
    AutoLock a(&lock_);
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
            return MX_ERR_NEXT;
        });
    return count;
}

status_t VmObjectPaged::AddPage(vm_page_t* p, uint64_t offset) {
    AutoLock a(&lock_);

    return AddPageLocked(p, offset);
}

status_t VmObjectPaged::AddPageLocked(vm_page_t* p, uint64_t offset) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.IsHeld());

    LTRACEF("vmo %p, offset %#" PRIx64 ", page %p (%#" PRIxPTR ")\n", this, offset, p, vm_page_to_paddr(p));

    DEBUG_ASSERT(p);

    if (offset >= size_)
        return MX_ERR_OUT_OF_RANGE;

    status_t err = page_list_.AddPage(p, offset);
    if (err != MX_OK)
        return err;

    // other mappings may have covered this offset into the vmo, so unmap those ranges
    RangeChangeUpdateLocked(offset, PAGE_SIZE);

    return MX_OK;
}

mx_status_t VmObjectPaged::CreateFromROData(const void* data, size_t size, fbl::RefPtr<VmObject>* obj) {
    fbl::RefPtr<VmObject> vmo;
    mx_status_t status = Create(PMM_ALLOC_FLAG_ANY, size, &vmo);
    if (status != MX_OK)
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

        // TODO(mcgrathr): If the last reference to this VMO were released
        // so the VMO got destroyed, that would attempt to return these
        // pages to the system.  On arm and arm64, the kernel cannot
        // tolerate a hole being created in the kernel image mapping, so
        // bad things happen.  Until that issue is fixed, just leak a
        // reference here so that the new VMO will never be destroyed.
        vmo.reset(vmo.leak_ref());
    }

    *obj = fbl::move(vmo);

    return MX_OK;
}

// Looks up the page at the requested offset, faulting it in if requested and necessary.  If
// this VMO has a parent and the requested page isn't found, the parent will be searched.
//
// |free_list|, if not NULL, is a list of allocated but unused vm_page_t that
// this function may allocate from.  This function will need at most one entry,
// and will not fail if |free_list| is a non-empty list, faulting in was requested,
// and offset is in range.
status_t VmObjectPaged::GetPageLocked(uint64_t offset, uint pf_flags, list_node* free_list,
                                      vm_page_t** const page_out, paddr_t* const pa_out) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.IsHeld());

    if (offset >= size_)
        return MX_ERR_OUT_OF_RANGE;

    vm_page_t* p;
    paddr_t pa;

    // see if we already have a page at that offset
    p = page_list_.GetPage(offset);
    if (p) {
        if (page_out)
            *page_out = p;
        if (pa_out)
            *pa_out = vm_page_to_paddr(p);
        return MX_OK;
    }

    __UNUSED char pf_string[5];
    LTRACEF("vmo %p, offset %#" PRIx64 ", pf_flags %#x (%s)\n", this, offset, pf_flags,
            vmm_pf_flags_to_string(pf_flags, pf_string));

    // if we have a parent see if they have a page for us
    if (parent_) {
        safeint::CheckedNumeric<uint64_t> parent_offset = parent_offset_;
        parent_offset += offset;
        DEBUG_ASSERT(parent_offset.IsValid());

        // make sure we don't cause the parent to fault in new pages, just ask for any that already exist
        uint parent_pf_flags = pf_flags & ~(VMM_PF_FLAG_FAULT_MASK);

        status_t status = parent_->GetPageLocked(parent_offset.ValueOrDie(), parent_pf_flags,
                                                 nullptr, &p, &pa);
        if (status == MX_OK) {
            // we have a page from them. if we're read-only faulting, return that page so they can map
            // or read from it directly
            if ((pf_flags & VMM_PF_FLAG_WRITE) == 0) {
                if (page_out)
                    *page_out = p;
                if (pa_out)
                    *pa_out = pa;

                LTRACEF("read only faulting in page %p, pa %#" PRIxPTR " from parent\n", p, pa);

                return MX_OK;
            }

            // if we're write faulting, we need to clone it and return the new page
            paddr_t pa_clone;
            vm_page_t* p_clone = nullptr;
            if (free_list) {
                p_clone = list_remove_head_type(free_list, vm_page_t, free.node);
                if (p_clone) {
                    pa_clone = vm_page_to_paddr(p_clone);
                }
            }
            if (!p_clone) {
                p_clone = pmm_alloc_page(pmm_alloc_flags_, &pa_clone);
            }
            if (!p_clone) {
                return MX_ERR_NO_MEMORY;
            }

            InitializeVmPage(p_clone);

            // do a direct copy of the two pages
            const void* src = paddr_to_kvaddr(pa);
            void* dst = paddr_to_kvaddr(pa_clone);

            DEBUG_ASSERT(src && dst);

            memcpy(dst, src, PAGE_SIZE);

            // add the new page and return it
            status = AddPageLocked(p_clone, offset);
            DEBUG_ASSERT(status == MX_OK);

            LTRACEF("copy-on-write faulted in page %p, pa %#" PRIxPTR " copied from %p, pa %#" PRIxPTR "\n",
                    p, pa, p_clone, pa_clone);

            if (page_out)
                *page_out = p_clone;
            if (pa_out)
                *pa_out = pa_clone;

            return MX_OK;
        }
    }

    // if we're not being asked to sw or hw fault in the page, return not found
    if ((pf_flags & VMM_PF_FLAG_FAULT_MASK) == 0)
        return MX_ERR_NOT_FOUND;

    // if we're read faulting, we don't already have a page, and the parent doesn't have it,
    // return the single global zero page
    if ((pf_flags & VMM_PF_FLAG_WRITE) == 0) {
        LTRACEF("returning the zero page\n");
        if (page_out)
            *page_out = vm_get_zero_page();
        if (pa_out)
            *pa_out = vm_get_zero_page_paddr();
        return MX_OK;
    }

    // allocate a page
    if (free_list) {
        p = list_remove_head_type(free_list, vm_page_t, free.node);
        if (p) {
            pa = vm_page_to_paddr(p);
        }
    }
    if (!p) {
        p = pmm_alloc_page(pmm_alloc_flags_, &pa);
    }
    if (!p) {
        return MX_ERR_NO_MEMORY;
    }

    InitializeVmPage(p);

    // TODO: remove once pmm returns zeroed pages
    ZeroPage(pa);

    status_t status = AddPageLocked(p, offset);
    DEBUG_ASSERT(status == MX_OK);

    // other mappings may have covered this offset into the vmo, so unmap those ranges
    RangeChangeUpdateLocked(offset, PAGE_SIZE);

    LTRACEF("faulted in page %p, pa %#" PRIxPTR "\n", p, pa);

    if (page_out)
        *page_out = p;
    if (pa_out)
        *pa_out = pa;

    return MX_OK;
}

status_t VmObjectPaged::CommitRange(uint64_t offset, uint64_t len, uint64_t* committed) {
    canary_.Assert();
    LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 "\n", offset, len);

    if (committed)
        *committed = 0;

    AutoLock a(&lock_);

    // trim the size
    uint64_t new_len;
    if (!TrimRange(offset, len, size_, &new_len))
        return MX_ERR_OUT_OF_RANGE;

    // was in range, just zero length
    if (new_len == 0)
        return MX_OK;

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
            return MX_ERR_NEXT;
        },
        expected_next_off, end);

    // If expected_next_off isn't at the end of the range, there was a gap at
    // the end.  Add it back in
    DEBUG_ASSERT(end >= expected_next_off);
    count += (end - expected_next_off) / PAGE_SIZE;
    if (count == 0)
        return MX_OK;

    // allocate count number of pages
    list_node page_list;
    list_initialize(&page_list);

    size_t allocated = pmm_alloc_pages(count, pmm_alloc_flags_, &page_list);
    if (allocated < count) {
        LTRACEF("failed to allocate enough pages (asked for %zu, got %zu)\n", count, allocated);
        pmm_free(&page_list);
        return MX_ERR_NO_MEMORY;
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
        status_t status = GetPageLocked(o, flags, &page_list, &p, &pa);
        ASSERT(status == MX_OK);

        if (committed)
            *committed += PAGE_SIZE;
    }

    DEBUG_ASSERT(list_is_empty(&page_list));

    // for now we only support committing as much as we were asked for
    DEBUG_ASSERT(!committed || *committed == count * PAGE_SIZE);

    return MX_OK;
}

status_t VmObjectPaged::CommitRangeContiguous(uint64_t offset, uint64_t len, uint64_t* committed,
                                              uint8_t alignment_log2) {
    canary_.Assert();
    LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 ", alignment %hhu\n", offset, len, alignment_log2);

    if (committed)
        *committed = 0;

    AutoLock a(&lock_);

    // This function does not support cloned VMOs.
    if (unlikely(parent_)) {
        return MX_ERR_NOT_SUPPORTED;
    }

    // trim the size
    uint64_t new_len;
    if (!TrimRange(offset, len, size_, &new_len))
        return MX_ERR_OUT_OF_RANGE;

    // was in range, just zero length
    if (new_len == 0)
        return MX_OK;

    // compute a page aligned end to do our searches in to make sure we cover all the pages
    uint64_t end = ROUNDUP_PAGE_SIZE(offset + new_len);
    DEBUG_ASSERT(end > offset);

    // make a pass through the list, making sure we have an empty run on the object
    size_t count = 0;
    for (uint64_t o = offset; o < end; o += PAGE_SIZE) {
        if (!page_list_.GetPage(o))
            count++;
    }

    DEBUG_ASSERT(count == new_len / PAGE_SIZE);
    if (count != new_len / PAGE_SIZE) {
        return MX_ERR_BAD_STATE;
    }

    // allocate count number of pages
    list_node page_list;
    list_initialize(&page_list);

    size_t allocated = pmm_alloc_contiguous(count, pmm_alloc_flags_, alignment_log2, nullptr, &page_list);
    if (allocated < count) {
        LTRACEF("failed to allocate enough pages (asked for %zu, got %zu)\n", count, allocated);
        pmm_free(&page_list);
        return MX_ERR_NO_MEMORY;
    }

    DEBUG_ASSERT(list_length(&page_list) == allocated);

    // unmap all of the pages in this range on all the mapping regions
    RangeChangeUpdateLocked(offset, end - offset);

    // add them to the appropriate range of the object
    for (uint64_t o = offset; o < end; o += PAGE_SIZE) {
        vm_page_t* p = list_remove_head_type(&page_list, vm_page_t, free.node);
        ASSERT(p);

        InitializeVmPage(p);

        // TODO: remove once pmm returns zeroed pages
        ZeroPage(p);

        auto status = page_list_.AddPage(p, o);
        DEBUG_ASSERT(status == MX_OK);

        // Mark the pages as pinned, so they can't be physically rearranged
        // underneath us.
        p->object.pin_count++;
        p->object.contiguous_pin = true;

        if (committed)
            *committed += PAGE_SIZE;
    }

    // for now we only support committing as much as we were asked for
    DEBUG_ASSERT(!committed || *committed == count * PAGE_SIZE);

    return MX_OK;
}

status_t VmObjectPaged::DecommitRange(uint64_t offset, uint64_t len, uint64_t* decommitted) {
    canary_.Assert();
    LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 "\n", offset, len);

    if (decommitted)
        *decommitted = 0;

    AutoLock a(&lock_);

    // trim the size
    uint64_t new_len;
    if (!TrimRange(offset, len, size_, &new_len))
        return MX_ERR_OUT_OF_RANGE;

    // was in range, just zero length
    if (new_len == 0)
        return MX_OK;

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
        return MX_ERR_BAD_STATE;
    }

    // unmap all of the pages in this range on all the mapping regions
    RangeChangeUpdateLocked(start, page_aligned_len);

    // iterate through the pages, freeing them
    while (start < end) {
        auto status = page_list_.FreePage(start);
        if (status == MX_OK && decommitted) {
            *decommitted += PAGE_SIZE;
        }
        start += PAGE_SIZE;
    }

    return MX_OK;
}

status_t VmObjectPaged::Pin(uint64_t offset, uint64_t len) {
    canary_.Assert();

    AutoLock a(&lock_);
    return PinLocked(offset, len);
}

status_t VmObjectPaged::PinLocked(uint64_t offset, uint64_t len) {
    canary_.Assert();

    // verify that the range is within the object
    if (unlikely(!InRange(offset, len, size_)))
        return MX_ERR_OUT_OF_RANGE;

    if (unlikely(len == 0))
        return MX_OK;

    const uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
    const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

    uint64_t expected_next_off = start_page_offset;
    status_t status = page_list_.ForEveryPageInRange(
        [&expected_next_off](const auto p, uint64_t off) {
            if (off != expected_next_off) {
                return MX_ERR_NOT_FOUND;
            }

            DEBUG_ASSERT(p->state == VM_PAGE_STATE_OBJECT);
            if (p->object.pin_count == VM_PAGE_OBJECT_MAX_PIN_COUNT) {
                return MX_ERR_UNAVAILABLE;
            }

            p->object.pin_count++;
            expected_next_off = off + PAGE_SIZE;
            return MX_ERR_NEXT;
        },
        start_page_offset, end_page_offset);

    if (status == MX_OK && expected_next_off != end_page_offset) {
        status = MX_ERR_NOT_FOUND;
    }
    if (status != MX_OK) {
        UnpinLocked(start_page_offset, expected_next_off - start_page_offset);
        return status;
    }

    return MX_OK;
}

void VmObjectPaged::Unpin(uint64_t offset, uint64_t len) {
    AutoLock a(&lock_);
    UnpinLocked(offset, len);
}

void VmObjectPaged::UnpinLocked(uint64_t offset, uint64_t len) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.IsHeld());

    // verify that the range is within the object
    ASSERT(InRange(offset, len, size_));

    if (unlikely(len == 0))
        return;

    const uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
    const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

    uint64_t expected_next_off = start_page_offset;
    status_t status = page_list_.ForEveryPageInRange(
        [&expected_next_off](const auto p, uint64_t off) {
            if (off != expected_next_off) {
                return MX_ERR_NOT_FOUND;
            }

            DEBUG_ASSERT(p->state == VM_PAGE_STATE_OBJECT);
            ASSERT(p->object.pin_count > 0);
            p->object.pin_count--;
            expected_next_off = off + PAGE_SIZE;
            return MX_ERR_NEXT;
        },
        start_page_offset, end_page_offset);
    ASSERT_MSG(status == MX_OK && expected_next_off == end_page_offset,
               "Tried to unpin an uncommitted page");
    return;
}

bool VmObjectPaged::AnyPagesPinnedLocked(uint64_t offset, size_t len) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.IsHeld());
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
                return MX_ERR_STOP;
            }
            return MX_ERR_NEXT;
        },
        start_page_offset, end_page_offset);

    return found_pinned;
}

status_t VmObjectPaged::ResizeLocked(uint64_t s) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.IsHeld());

    LTRACEF("vmo %p, size %" PRIu64 "\n", this, s);

    // there's a max size to keep indexes within range
    if (s > MAX_SIZE)
        return MX_ERR_OUT_OF_RANGE;

    // see if we're shrinking or expanding the vmo
    if (s < size_) {
        // shrinking
        // figure the starting and ending page offset that is affected
        uint64_t start = ROUNDUP_PAGE_SIZE(s);
        uint64_t end = ROUNDUP_PAGE_SIZE(size_);
        uint64_t page_aligned_len = end - start;

        // we're only worried about whole pages to be removed
        if (page_aligned_len > 0) {
            if (AnyPagesPinnedLocked(start, page_aligned_len)) {
                return MX_ERR_BAD_STATE;
            }
            // unmap all of the pages in this range on all the mapping regions
            RangeChangeUpdateLocked(start, page_aligned_len);

            // iterate through the pages, freeing them
            while (start < end) {
                page_list_.FreePage(start);
                start += PAGE_SIZE;
            }
        }
    } else if (s > size_) {
        // expanding
        // figure the starting and ending page offset that is affected
        uint64_t start = ROUNDUP_PAGE_SIZE(size_);
        uint64_t end = ROUNDUP_PAGE_SIZE(s);
        uint64_t page_aligned_len = end - start;

        // we're only worried about whole pages to be added
        if (page_aligned_len > 0) {
            // inform all our children or mapping that there's new bits
            RangeChangeUpdateLocked(start, page_aligned_len);
        }
    }

    // save bytewise size
    size_ = s;

    return MX_OK;
}

status_t VmObjectPaged::Resize(uint64_t s) {
    AutoLock a(&lock_);

    return ResizeLocked(s);
}

status_t VmObjectPaged::SetParentOffsetLocked(uint64_t offset) {
    DEBUG_ASSERT(lock_.IsHeld());

    // offset must be page aligned
    if (!IS_PAGE_ALIGNED(offset))
        return MX_ERR_INVALID_ARGS;

    // TODO: MG-692 make sure that the accumulated offset of the entire parent chain doesn't wrap 64bit space

    // make sure the size + this offset are still valid
    safeint::CheckedNumeric<uint64_t> end = offset;
    end += size_;
    if (!end.IsValid())
        return MX_ERR_OUT_OF_RANGE;

    parent_offset_ = offset;

    return MX_OK;
}

// perform some sort of copy in/out on a range of the object using a passed in lambda
// for the copy routine
template <typename T>
status_t VmObjectPaged::ReadWriteInternal(uint64_t offset, size_t len, size_t* bytes_copied, bool write,
                                          T copyfunc) {
    canary_.Assert();
    if (bytes_copied)
        *bytes_copied = 0;

    AutoLock a(&lock_);

    // trim the size
    uint64_t new_len;
    if (!TrimRange(offset, len, size_, &new_len))
        return MX_ERR_OUT_OF_RANGE;

    // was in range, just zero length
    if (new_len == 0)
        return 0;

    // walk the list of pages and do the write
    uint64_t src_offset = offset;
    size_t dest_offset = 0;
    while (new_len > 0) {
        size_t page_offset = src_offset % PAGE_SIZE;
        size_t tocopy = MIN(PAGE_SIZE - page_offset, new_len);

        // fault in the page
        paddr_t pa;
        auto status = GetPageLocked(src_offset,
                                    VMM_PF_FLAG_SW_FAULT | (write ? VMM_PF_FLAG_WRITE : 0),
                                    nullptr, nullptr, &pa);
        if (status < 0)
            return status;

        // compute the kernel mapping of this page
        uint8_t* page_ptr = reinterpret_cast<uint8_t*>(paddr_to_kvaddr(pa));

        // call the copy routine
        auto err = copyfunc(page_ptr + page_offset, dest_offset, tocopy);
        if (err < 0)
            return err;

        src_offset += tocopy;
        if (bytes_copied)
            *bytes_copied += tocopy;
        dest_offset += tocopy;
        new_len -= tocopy;
    }

    return MX_OK;
}

status_t VmObjectPaged::Read(void* _ptr, uint64_t offset, size_t len, size_t* bytes_read) {
    canary_.Assert();
    // test to make sure this is a kernel pointer
    if (!is_kernel_address(reinterpret_cast<vaddr_t>(_ptr))) {
        DEBUG_ASSERT_MSG(0, "non kernel pointer passed\n");
        return MX_ERR_INVALID_ARGS;
    }

    // read routine that just uses a memcpy
    uint8_t* ptr = reinterpret_cast<uint8_t*>(_ptr);
    auto read_routine = [ptr](const void* src, size_t offset, size_t len) -> status_t {
        memcpy(ptr + offset, src, len);
        return MX_OK;
    };

    return ReadWriteInternal(offset, len, bytes_read, false, read_routine);
}

status_t VmObjectPaged::Write(const void* _ptr, uint64_t offset, size_t len, size_t* bytes_written) {
    canary_.Assert();
    // test to make sure this is a kernel pointer
    if (!is_kernel_address(reinterpret_cast<vaddr_t>(_ptr))) {
        DEBUG_ASSERT_MSG(0, "non kernel pointer passed\n");
        return MX_ERR_INVALID_ARGS;
    }

    // write routine that just uses a memcpy
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(_ptr);
    auto write_routine = [ptr](void* dst, size_t offset, size_t len) -> status_t {
        memcpy(dst, ptr + offset, len);
        return MX_OK;
    };

    return ReadWriteInternal(offset, len, bytes_written, true, write_routine);
}

status_t VmObjectPaged::Lookup(uint64_t offset, uint64_t len, uint pf_flags,
                               vmo_lookup_fn_t lookup_fn, void* context) {
    canary_.Assert();
    if (unlikely(len == 0))
        return MX_ERR_INVALID_ARGS;

    AutoLock a(&lock_);

    // verify that the range is within the object
    if (unlikely(!InRange(offset, len, size_)))
        return MX_ERR_OUT_OF_RANGE;

    const uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
    const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

    uint64_t expected_next_off = start_page_offset;
    status_t status = page_list_.ForEveryPageInRange(
        [&expected_next_off, this, pf_flags, lookup_fn, context,
         start_page_offset](const auto p, uint64_t off) {

            // If some page was missing from our list, run the more expensive
            // GetPageLocked to see if our parent has it.
            for (uint64_t missing_off = expected_next_off; missing_off < off;
                 missing_off += PAGE_SIZE) {

                paddr_t pa;
                status_t status = this->GetPageLocked(missing_off, pf_flags, nullptr,
                                                      nullptr, &pa);
                if (status != MX_OK) {
                    return MX_ERR_NO_MEMORY;
                }
                const size_t index = (off - start_page_offset) / PAGE_SIZE;
                status = lookup_fn(context, missing_off, index, pa);
                if (status != MX_OK) {
                    if (unlikely(status == MX_ERR_NEXT || status == MX_ERR_STOP)) {
                        status = MX_ERR_INTERNAL;
                    }
                    return status;
                }
            }

            const size_t index = (off - start_page_offset) / PAGE_SIZE;
            paddr_t pa = vm_page_to_paddr(p);
            status_t status = lookup_fn(context, off, index, pa);
            if (status != MX_OK) {
                if (unlikely(status == MX_ERR_NEXT || status == MX_ERR_STOP)) {
                    status = MX_ERR_INTERNAL;
                }
                return status;
            }

            expected_next_off = off + PAGE_SIZE;
            return MX_ERR_NEXT;
        },
        start_page_offset, end_page_offset);
    if (status != MX_OK) {
        return status;
    }

    // If expected_next_off isn't at the end, there's a gap to process
    for (uint64_t off = expected_next_off; off < end_page_offset; off += PAGE_SIZE) {
        paddr_t pa;
        status_t status = GetPageLocked(off, pf_flags, nullptr, nullptr, &pa);
        if (status != MX_OK) {
            return MX_ERR_NO_MEMORY;
        }
        const size_t index = (off - start_page_offset) / PAGE_SIZE;
        status = lookup_fn(context, off, index, pa);
        if (status != MX_OK) {
            return status;
        }
    }

    return MX_OK;
}

status_t VmObjectPaged::ReadUser(user_ptr<void> ptr, uint64_t offset, size_t len, size_t* bytes_read) {
    canary_.Assert();

    // read routine that uses copy_to_user
    auto read_routine = [ptr](const void* src, size_t offset, size_t len) -> status_t {
        return ptr.byte_offset(offset).copy_array_to_user(src, len);
    };

    return ReadWriteInternal(offset, len, bytes_read, false, read_routine);
}

status_t VmObjectPaged::WriteUser(user_ptr<const void> ptr, uint64_t offset, size_t len,
                                  size_t* bytes_written) {
    canary_.Assert();

    // write routine that uses copy_from_user
    auto write_routine = [ptr](void* dst, size_t offset, size_t len) -> status_t {
        return ptr.byte_offset(offset).copy_array_from_user(dst, len);
    };

    return ReadWriteInternal(offset, len, bytes_written, true, write_routine);
}

status_t VmObjectPaged::LookupUser(uint64_t offset, uint64_t len, user_ptr<paddr_t> buffer,
                                   size_t buffer_size) {
    canary_.Assert();

    uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
    uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);
    // compute the size of the table we'll need and make sure it fits in the user buffer
    uint64_t table_size = ((end_page_offset - start_page_offset) / PAGE_SIZE) * sizeof(paddr_t);
    if (unlikely(table_size > buffer_size))
        return MX_ERR_BUFFER_TOO_SMALL;

    auto copy_to_user = [](void* context, size_t offset, size_t index, paddr_t pa) -> status_t {
        user_ptr<paddr_t>* buffer = static_cast<user_ptr<paddr_t>*>(context);
        return buffer->element_offset(index).copy_to_user(pa);
    };
    // only lookup pages that are already present
    return Lookup(offset, len, 0, copy_to_user, &buffer);
}

status_t VmObjectPaged::InvalidateCache(const uint64_t offset, const uint64_t len) {
    return CacheOp(offset, len, CacheOpType::Invalidate);
}

status_t VmObjectPaged::CleanCache(const uint64_t offset, const uint64_t len) {
    return CacheOp(offset, len, CacheOpType::Clean);
}

status_t VmObjectPaged::CleanInvalidateCache(const uint64_t offset, const uint64_t len) {
    return CacheOp(offset, len, CacheOpType::CleanInvalidate);
}

status_t VmObjectPaged::SyncCache(const uint64_t offset, const uint64_t len) {
    return CacheOp(offset, len, CacheOpType::Sync);
}

status_t VmObjectPaged::CacheOp(const uint64_t start_offset, const uint64_t len,
                                const CacheOpType type) {
    canary_.Assert();

    if (unlikely(len == 0))
        return MX_ERR_INVALID_ARGS;

    AutoLock a(&lock_);

    if (unlikely(!InRange(start_offset, len, size_)))
        return MX_ERR_OUT_OF_RANGE;

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

        if (likely(status == MX_OK)) {
            // Convert the page address to a Kernel virtual address.
            const void* ptr = paddr_to_kvaddr(pa);
            const addr_t cache_op_addr = reinterpret_cast<addr_t>(ptr) + page_offset;

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

    return MX_OK;
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
