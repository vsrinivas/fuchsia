// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "kernel/vm/vm_object_paged.h"

#include "vm_priv.h"

#include <arch/ops.h>
#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/auto_lock.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_address_region.h>
#include <lib/console.h>
#include <lib/user_copy.h>
#include <mxalloc/new.h>
#include <safeint/safe_math.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

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

} // namespace

VmObjectPaged::VmObjectPaged(uint32_t pmm_alloc_flags, mxtl::RefPtr<VmObject> parent)
    : VmObject(mxtl::move(parent)), pmm_alloc_flags_(pmm_alloc_flags) {
    LTRACEF("%p\n", this);
}

VmObjectPaged::~VmObjectPaged() {
    canary_.Assert();

    LTRACEF("%p\n", this);

    // free all of the pages attached to us
    page_list_.FreeAllPages();
}

mxtl::RefPtr<VmObject> VmObjectPaged::Create(uint32_t pmm_alloc_flags, uint64_t size) {
    // there's a max size to keep indexes within range
    if (size > MAX_SIZE)
        return nullptr;

    AllocChecker ac;
    auto vmo = mxtl::AdoptRef<VmObject>(new (&ac) VmObjectPaged(pmm_alloc_flags, nullptr));
    if (!ac.check())
        return nullptr;

    auto err = vmo->Resize(size);
    if (err == ERR_NO_MEMORY)
        return nullptr;
    // Other kinds of failures are not handled yet.
    DEBUG_ASSERT(err == NO_ERROR);
    if (err != NO_ERROR)
        return nullptr;

    return vmo;
}

status_t VmObjectPaged::CloneCOW(uint64_t offset, uint64_t size, mxtl::RefPtr<VmObject>* clone_vmo) {
    LTRACEF("vmo %p offset %#" PRIx64 " size %#" PRIx64 "\n", this, offset, size);

    canary_.Assert();

    AllocChecker ac;
    auto vmo = mxtl::AdoptRef<VmObjectPaged>(new (&ac) VmObjectPaged(pmm_alloc_flags_, mxtl::WrapRefPtr(this)));
    if (!ac.check())
        return ERR_NO_MEMORY;

    AutoLock a(&lock_);

    // add it as a child to us
    AddChildLocked(vmo.get());

    // set the new clone's size
    auto status = vmo->ResizeLocked(size);
    if (status != NO_ERROR)
        return status;

    // set the offset with the parent
    status = vmo->SetParentOffsetLocked(offset);
    if (status != NO_ERROR)
        return status;

    *clone_vmo = mxtl::move(vmo);

    return NO_ERROR;
}

void VmObjectPaged::Dump(uint depth, bool verbose) {
    canary_.Assert();

    AutoLock a(&lock_);

    size_t count = 0;
    page_list_.ForEveryPage([&count](const auto p, uint64_t) { count++; });

    for (uint i = 0; i < depth; ++i) {
        printf("  ");
    }
    printf("object %p size %#" PRIx64 " pages %zu ref %d\n", this, size_, count, ref_count_debug());

    if (verbose) {
        auto f = [depth](const auto p, uint64_t offset) {
            for (uint i = 0; i < depth + 1; ++i) {
                printf("  ");
            }
            printf("offset %#" PRIx64 " page %p paddr %#" PRIxPTR "\n", offset, p, vm_page_to_paddr(p));
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
        return ERR_OUT_OF_RANGE;

    status_t err = page_list_.AddPage(p, offset);
    if (err != NO_ERROR)
        return err;

    // other mappings may have covered this offset into the vmo, so unmap those ranges
    RangeChangeUpdateLocked(offset, PAGE_SIZE);

    return NO_ERROR;
}

mxtl::RefPtr<VmObject> VmObjectPaged::CreateFromROData(const void* data, size_t size) {
    auto vmo = Create(PMM_ALLOC_FLAG_ANY, size);
    if (vmo && size > 0) {
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

    return vmo;
}

status_t VmObjectPaged::GetPageLocked(uint64_t offset, uint pf_flags, vm_page_t** const page_out, paddr_t* const pa_out) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.IsHeld());

    if (offset >= size_)
        return ERR_OUT_OF_RANGE;

    vm_page_t* p;
    paddr_t pa;

    // see if we already have a page at that offset
    p = page_list_.GetPage(offset);
    if (p) {
        if (page_out)
            *page_out = p;
        if (pa_out)
            *pa_out = vm_page_to_paddr(p);
        return NO_ERROR;
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

        status_t status = parent_->GetPageLocked(parent_offset.ValueOrDie(), parent_pf_flags, &p, &pa);
        if (status == NO_ERROR) {
            // we have a page from them. if we're read-only faulting, return that page so they can map
            // or read from it directly
            if ((pf_flags & VMM_PF_FLAG_WRITE) == 0) {
                if (page_out)
                    *page_out = p;
                if (pa_out)
                    *pa_out = pa;

                LTRACEF("read only faulting in page %p, pa %#" PRIxPTR " from parent\n", p, pa);

                return NO_ERROR;
            }

            // if we're write faulting, we need to clone it and return the new page
            paddr_t pa_clone;
            vm_page_t* p_clone = pmm_alloc_page(pmm_alloc_flags_, &pa_clone);
            if (!p_clone)
                return ERR_NO_MEMORY;

            p_clone->state = VM_PAGE_STATE_OBJECT;

            // do a direct copy of the two pages
            const void* src = paddr_to_kvaddr(pa);
            void* dst = paddr_to_kvaddr(pa_clone);

            DEBUG_ASSERT(src && dst);

            memcpy(dst, src, PAGE_SIZE);

            // add the new page and return it
            status = AddPageLocked(p_clone, offset);
            DEBUG_ASSERT(status == NO_ERROR);

            LTRACEF("copy-on-write faulted in page %p, pa %#" PRIxPTR " copied from %p, pa %#" PRIxPTR "\n",
                    p, pa, p_clone, pa_clone);

            if (page_out)
                *page_out = p_clone;
            if (pa_out)
                *pa_out = pa_clone;

            return NO_ERROR;
        }
    }

    // if we're not being asked to sw or hw fault in the page, return not found
    if ((pf_flags & VMM_PF_FLAG_FAULT_MASK) == 0)
        return ERR_NOT_FOUND;

    // if we're read faulting, we don't already have a page, and the parent doesn't have it,
    // return the single global zero page
    if ((pf_flags & VMM_PF_FLAG_WRITE) == 0) {
        LTRACEF("returning the zero page\n");
        if (page_out)
            *page_out = vm_get_zero_page();
        if (pa_out)
            *pa_out = vm_get_zero_page_paddr();
        return NO_ERROR;
    }

    // allocate a page
    p = pmm_alloc_page(pmm_alloc_flags_, &pa);
    if (!p)
        return ERR_NO_MEMORY;

    p->state = VM_PAGE_STATE_OBJECT;

    // TODO: remove once pmm returns zeroed pages
    ZeroPage(pa);

    status_t status = AddPageLocked(p, offset);
    DEBUG_ASSERT(status == NO_ERROR);

    // other mappings may have covered this offset into the vmo, so unmap those ranges
    RangeChangeUpdateLocked(offset, PAGE_SIZE);

    LTRACEF("faulted in page %p, pa %#" PRIxPTR "\n", p, pa);

    if (page_out)
        *page_out = p;
    if (pa_out)
        *pa_out = pa;

    return NO_ERROR;
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
        return ERR_OUT_OF_RANGE;

    // was in range, just zero length
    if (new_len == 0)
        return NO_ERROR;

    // compute a page aligned end to do our searches in to make sure we cover all the pages
    uint64_t end = ROUNDUP_PAGE_SIZE(offset + new_len);
    DEBUG_ASSERT(end > offset);

    // make a pass through the list, counting the number of pages we need to allocate
    size_t count = 0;
    for (uint64_t o = offset; o < end; o += PAGE_SIZE) {
        if (!page_list_.GetPage(o))
            count++;
    }
    if (count == 0)
        return NO_ERROR;

    // allocate count number of pages
    list_node page_list;
    list_initialize(&page_list);

    size_t allocated = pmm_alloc_pages(count, pmm_alloc_flags_, &page_list);
    if (allocated < count) {
        LTRACEF("failed to allocate enough pages (asked for %zu, got %zu)\n", count, allocated);
        pmm_free(&page_list);
        return ERR_NO_MEMORY;
    }

    // unmap all of the pages in this range on all the mapping regions
    RangeChangeUpdateLocked(offset, end - offset);

    // add them to the appropriate range of the object
    for (uint64_t o = offset; o < end; o += PAGE_SIZE) {
        vm_page_t* p = page_list_.GetPage(o);
        if (p)
            continue;

        p = list_remove_head_type(&page_list, vm_page_t, free.node);
        ASSERT(p);

        p->state = VM_PAGE_STATE_OBJECT;

        // TODO: remove once pmm returns zeroed pages
        ZeroPage(p);

        status_t status = page_list_.AddPage(p, o);
        DEBUG_ASSERT(status == NO_ERROR);

        if (committed)
            *committed += PAGE_SIZE;
    }

    DEBUG_ASSERT(list_is_empty(&page_list));

    // for now we only support committing as much as we were asked for
    DEBUG_ASSERT(!committed || *committed == count * PAGE_SIZE);

    return NO_ERROR;
}

status_t VmObjectPaged::CommitRangeContiguous(uint64_t offset, uint64_t len, uint64_t* committed,
                                              uint8_t alignment_log2) {
    canary_.Assert();
    LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 ", alignment %hhu\n", offset, len, alignment_log2);

    if (committed)
        *committed = 0;

    AutoLock a(&lock_);

    // trim the size
    uint64_t new_len;
    if (!TrimRange(offset, len, size_, &new_len))
        return ERR_OUT_OF_RANGE;

    // was in range, just zero length
    if (new_len == 0)
        return NO_ERROR;

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

    // allocate count number of pages
    list_node page_list;
    list_initialize(&page_list);

    size_t allocated = pmm_alloc_contiguous(count, pmm_alloc_flags_, alignment_log2, nullptr, &page_list);
    if (allocated < count) {
        LTRACEF("failed to allocate enough pages (asked for %zu, got %zu)\n", count, allocated);
        pmm_free(&page_list);
        return ERR_NO_MEMORY;
    }

    DEBUG_ASSERT(list_length(&page_list) == allocated);

    // unmap all of the pages in this range on all the mapping regions
    RangeChangeUpdateLocked(offset, end - offset);

    // add them to the appropriate range of the object
    for (uint64_t o = offset; o < end; o += PAGE_SIZE) {
        vm_page_t* p = list_remove_head_type(&page_list, vm_page_t, free.node);
        ASSERT(p);

        p->state = VM_PAGE_STATE_OBJECT;

        // TODO: remove once pmm returns zeroed pages
        ZeroPage(p);

        auto status = page_list_.AddPage(p, o);
        DEBUG_ASSERT(status == NO_ERROR);

        if (committed)
            *committed += PAGE_SIZE;
    }

    // for now we only support committing as much as we were asked for
    DEBUG_ASSERT(!committed || *committed == count * PAGE_SIZE);

    return NO_ERROR;
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
        return ERR_OUT_OF_RANGE;

    // was in range, just zero length
    if (new_len == 0)
        return NO_ERROR;

    // figure the starting and ending page offset
    uint64_t start = PAGE_ALIGN(offset);
    uint64_t end = ROUNDUP_PAGE_SIZE(offset + new_len);
    DEBUG_ASSERT(end > offset);
    DEBUG_ASSERT(end > start);
    uint64_t page_aligned_len = end - start;

    LTRACEF("start offset %#" PRIx64 ", end %#" PRIx64 ", page_aliged_len %#" PRIx64 "\n", start, end,
            page_aligned_len);

    // unmap all of the pages in this range on all the mapping regions
    RangeChangeUpdateLocked(start, page_aligned_len);

    // iterate through the pages, freeing them
    while (start < end) {
        auto status = page_list_.FreePage(start);
        if (status == NO_ERROR && decommitted) {
            *decommitted += PAGE_SIZE;
        }
        start += PAGE_SIZE;
    }

    return NO_ERROR;
}

status_t VmObjectPaged::ResizeLocked(uint64_t s) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.IsHeld());

    LTRACEF("vmo %p, size %" PRIu64 "\n", this, s);

    // there's a max size to keep indexes within range
    if (s > MAX_SIZE)
        return ERR_OUT_OF_RANGE;

    // see if we're shrinking or expanding the vmo
    if (s < size_) {
        // shrinking
        // figure the starting and ending page offset that is affected
        uint64_t start = ROUNDUP_PAGE_SIZE(s);
        uint64_t end = ROUNDUP_PAGE_SIZE(size_);
        uint64_t page_aligned_len = end - start;

        // we're only worried about whole pages to be removed
        if (page_aligned_len > 0) {
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

    return NO_ERROR;
}

status_t VmObjectPaged::Resize(uint64_t s) {
    AutoLock a(&lock_);

    return ResizeLocked(s);
}

status_t VmObjectPaged::SetParentOffsetLocked(uint64_t offset) {
    DEBUG_ASSERT(lock_.IsHeld());

    // offset must be page aligned
    if (!IS_PAGE_ALIGNED(offset))
        return ERR_INVALID_ARGS;

    // TODO: MG-692 make sure that the accumulated offset of the entire parent chain doesn't wrap 64bit space

    // make sure the size + this offset are still valid
    safeint::CheckedNumeric<uint64_t> end = offset;
    end += size_;
    if (!end.IsValid())
        return ERR_OUT_OF_RANGE;

    parent_offset_ = offset;

    return NO_ERROR;
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
        return ERR_OUT_OF_RANGE;

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
        auto status = GetPageLocked(src_offset, VMM_PF_FLAG_SW_FAULT | (write ? VMM_PF_FLAG_WRITE : 0), nullptr, &pa);
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

    return NO_ERROR;
}

status_t VmObjectPaged::Read(void* _ptr, uint64_t offset, size_t len, size_t* bytes_read) {
    canary_.Assert();
    // test to make sure this is a kernel pointer
    if (!is_kernel_address(reinterpret_cast<vaddr_t>(_ptr))) {
        DEBUG_ASSERT_MSG(0, "non kernel pointer passed\n");
        return ERR_INVALID_ARGS;
    }

    // read routine that just uses a memcpy
    uint8_t* ptr = reinterpret_cast<uint8_t*>(_ptr);
    auto read_routine = [ptr](const void* src, size_t offset, size_t len) -> status_t {
        memcpy(ptr + offset, src, len);
        return NO_ERROR;
    };

    return ReadWriteInternal(offset, len, bytes_read, false, read_routine);
}

status_t VmObjectPaged::Write(const void* _ptr, uint64_t offset, size_t len, size_t* bytes_written) {
    canary_.Assert();
    // test to make sure this is a kernel pointer
    if (!is_kernel_address(reinterpret_cast<vaddr_t>(_ptr))) {
        DEBUG_ASSERT_MSG(0, "non kernel pointer passed\n");
        return ERR_INVALID_ARGS;
    }

    // write routine that just uses a memcpy
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(_ptr);
    auto write_routine = [ptr](void* dst, size_t offset, size_t len) -> status_t {
        memcpy(dst, ptr + offset, len);
        return NO_ERROR;
    };

    return ReadWriteInternal(offset, len, bytes_written, true, write_routine);
}

status_t VmObjectPaged::Lookup(uint64_t offset, uint64_t len, uint pf_flags,
                               vmo_lookup_fn_t lookup_fn, void* context) {
    canary_.Assert();
    if (unlikely(len == 0))
        return ERR_INVALID_ARGS;

    AutoLock a(&lock_);

    // verify that the range is within the object
    if (unlikely(!InRange(offset, len, size_)))
        return ERR_OUT_OF_RANGE;

    uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
    uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

    size_t index = 0;
    for (uint64_t off = start_page_offset; off != end_page_offset; off += PAGE_SIZE, index++) {
        paddr_t pa;
        auto status = GetPageLocked(off, pf_flags, nullptr, &pa);
        if (status < 0)
            return ERR_NO_MEMORY;

        status = lookup_fn(context, off, index, pa);
        if (unlikely(status < 0))
            return status;
    }

    return NO_ERROR;
}

status_t VmObjectPaged::ReadUser(user_ptr<void> ptr, uint64_t offset, size_t len, size_t* bytes_read) {
    canary_.Assert();

    // test to make sure this is a user pointer
    if (!ptr.is_user_address()) {
        return ERR_INVALID_ARGS;
    }

    // read routine that uses copy_to_user
    auto read_routine = [ptr](const void* src, size_t offset, size_t len) -> status_t {
        return ptr.byte_offset(offset).copy_array_to_user(src, len);
    };

    return ReadWriteInternal(offset, len, bytes_read, false, read_routine);
}

status_t VmObjectPaged::WriteUser(user_ptr<const void> ptr, uint64_t offset, size_t len,
                                  size_t* bytes_written) {
    canary_.Assert();

    // test to make sure this is a user pointer
    if (!ptr.is_user_address()) {
        return ERR_INVALID_ARGS;
    }

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
        return ERR_BUFFER_TOO_SMALL;

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
        return ERR_INVALID_ARGS;

    AutoLock a(&lock_);

    if (unlikely(!InRange(start_offset, len, size_)))
        return ERR_OUT_OF_RANGE;

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
        auto status = GetPageLocked(op_start_offset, 0, nullptr, &pa);

        if (likely(status == NO_ERROR)) {
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

    return NO_ERROR;
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
