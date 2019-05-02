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
#include <ktl/move.h>
#include <lib/console.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <vm/bootreserve.h>
#include <vm/fault.h>
#include <vm/page_source.h>
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
    DEBUG_ASSERT(p->state() == VM_PAGE_STATE_ALLOC);
    p->set_state(VM_PAGE_STATE_OBJECT);
    p->object.pin_count = 0;
}

// round up the size to the next page size boundary and make sure we dont wrap
zx_status_t RoundSize(uint64_t size, uint64_t* out_size) {
    *out_size = ROUNDUP_PAGE_SIZE(size);
    if (*out_size < size) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    // there's a max size to keep indexes within range
    if (*out_size > VmObjectPaged::MAX_SIZE) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    return ZX_OK;
}

} // namespace

VmObjectPaged::VmObjectPaged(
    uint32_t options, uint32_t pmm_alloc_flags, uint64_t size,
    fbl::RefPtr<VmObject> parent, fbl::RefPtr<vm_lock_t> root_lock,
    fbl::RefPtr<PageSource> page_source)
    : VmObject(ktl::move(root_lock)),
      options_(options),
      size_(size),
      pmm_alloc_flags_(pmm_alloc_flags),
      parent_(ktl::move(parent)),
      page_source_(ktl::move(page_source)) {
    LTRACEF("%p\n", this);

    DEBUG_ASSERT(IS_PAGE_ALIGNED(size_));
    DEBUG_ASSERT(page_source_ == nullptr || parent_ == nullptr);

    // Adding to the global list needs to be done at the end of the ctor, since
    // calls can be made into this object as soon as it is in that list.
    AddToGlobalList();
}

VmObjectPaged::~VmObjectPaged() {
    canary_.Assert();

    LTRACEF("%p\n", this);

    RemoveFromGlobalList();

    page_list_.ForEveryPage(
        [this](const auto p, uint64_t off) {
            if (this->is_contiguous()) {
                p->object.pin_count--;
            }
            ASSERT(p->object.pin_count == 0);
            return ZX_ERR_NEXT;
        });

    // free all of the pages attached to us
    list_node_t list;
    list_initialize(&list);
    page_list_.RemoveAllPages(&list);

    if (page_source_) {
        page_source_->Close();
    }

    pmm_free(&list);

    // remove ourself from our parent (if present)
    if (parent_) {
        LTRACEF("removing ourself from our parent %p\n", parent_.get());
        parent_->RemoveChild(this);
    }
}

zx_status_t VmObjectPaged::CreateCommon(uint32_t pmm_alloc_flags,
                                        uint32_t options,
                                        uint64_t size, fbl::RefPtr<VmObject>* obj) {
    // make sure size is page aligned
    zx_status_t status = RoundSize(size, &size);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    auto lock = fbl::AdoptRef<vm_lock_t>(new (&ac) vm_lock_t);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto vmo = fbl::AdoptRef<VmObject>(
        new (&ac) VmObjectPaged(options, pmm_alloc_flags, size, nullptr, ktl::move(lock), nullptr));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    *obj = ktl::move(vmo);

    return ZX_OK;
}

zx_status_t VmObjectPaged::Create(uint32_t pmm_alloc_flags,
                                  uint32_t options,
                                  uint64_t size, fbl::RefPtr<VmObject>* obj) {
    if (options & kContiguous) {
        // Force callers to use CreateContiguous() instead.
        return ZX_ERR_INVALID_ARGS;
    }

    return CreateCommon(pmm_alloc_flags, options, size, obj);
}

zx_status_t VmObjectPaged::CreateContiguous(uint32_t pmm_alloc_flags, uint64_t size,
                                            uint8_t alignment_log2, fbl::RefPtr<VmObject>* obj) {
    DEBUG_ASSERT(alignment_log2 < sizeof(uint64_t) * 8);
    // make sure size is page aligned
    zx_status_t status = RoundSize(size, &size);
    if (status != ZX_OK) {
        return status;
    }

    fbl::RefPtr<VmObject> vmo;
    status = CreateCommon(pmm_alloc_flags, kContiguous, size, &vmo);
    if (status != ZX_OK) {
        return status;
    }

    if (size == 0) {
        *obj = ktl::move(vmo);
        return ZX_OK;
    }

    // allocate the pages
    list_node page_list;
    list_initialize(&page_list);

    size_t num_pages = size / PAGE_SIZE;
    paddr_t pa;
    status = pmm_alloc_contiguous(num_pages, pmm_alloc_flags, alignment_log2, &pa, &page_list);
    if (status != ZX_OK) {
        LTRACEF("failed to allocate enough pages (asked for %zu)\n", num_pages);
        return ZX_ERR_NO_MEMORY;
    }
    auto cleanup_phys_pages = fbl::MakeAutoCall([&page_list]() {
        pmm_free(&page_list);
    });

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
    *obj = ktl::move(vmo);
    return ZX_OK;
}

zx_status_t VmObjectPaged::CreateFromWiredPages(const void* data, size_t size, bool exclusive,
                                                fbl::RefPtr<VmObject>* obj) {
    LTRACEF("data %p, size %zu\n", data, size);

    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = CreateCommon(PMM_ALLOC_FLAG_ANY, 0, size, &vmo);
    if (status != ZX_OK) {
        return status;
    }

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

            if (page->state() == VM_PAGE_STATE_WIRED) {
                boot_reserve_unwire_page(page);
            } else {
                // This function is only valid for memory in the boot image,
                // which should all be wired.
                panic("page used to back static vmo in unusable state: paddr %#" PRIxPTR
                      " state %u\n", pa, page->state());
            }
            InitializeVmPage(page);

            // XXX hack to work around the ref pointer to the base class
            auto vmo2 = static_cast<VmObjectPaged*>(vmo.get());
            vmo2->AddPage(page, count * PAGE_SIZE);
        }
    }

    if (exclusive && !is_physmap_addr(data)) {
        // unmap it from the kernel
        // NOTE: this means the image can no longer be referenced from original pointer
        status = VmAspace::kernel_aspace()->arch_aspace().Unmap(
                reinterpret_cast<vaddr_t>(data), size / PAGE_SIZE, nullptr);
        ASSERT(status == ZX_OK);
    }

    *obj = ktl::move(vmo);

    return ZX_OK;
}

zx_status_t VmObjectPaged::CreateExternal(fbl::RefPtr<PageSource> src, uint32_t options,
                                          uint64_t size, fbl::RefPtr<VmObject>* obj) {
    // make sure size is page aligned
    zx_status_t status = RoundSize(size, &size);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    auto lock = fbl::AdoptRef<vm_lock_t>(new (&ac) vm_lock_t);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto vmo = fbl::AdoptRef<VmObject>(new (&ac) VmObjectPaged(
            options, PMM_ALLOC_FLAG_ANY, size, nullptr, ktl::move(lock), ktl::move(src)));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    *obj = ktl::move(vmo);

    return ZX_OK;
}

zx_status_t VmObjectPaged::CreateCowClone(bool resizable, uint64_t offset, uint64_t size,
                                          bool copy_name, fbl::RefPtr<VmObject>* child_vmo) {
    LTRACEF("vmo %p offset %#" PRIx64 " size %#" PRIx64 "\n", this, offset, size);

    canary_.Assert();

    // make sure size is page aligned
    zx_status_t status = RoundSize(size, &size);
    if (status != ZX_OK) {
        return status;
    }

    auto options = resizable ? kResizable : 0u;

    // allocate the clone up front outside of our lock to ensure that if we
    // return early, destroying the new vmo, it'll happen after the lock has
    // been released because we share the lock and the vmo's dtor may acquire it
    fbl::AllocChecker ac;
    auto vmo = fbl::AdoptRef<VmObjectPaged>(new (&ac) VmObjectPaged(
        options, pmm_alloc_flags_, size, fbl::WrapRefPtr(this), lock_ptr_, nullptr));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    uint32_t num_children;
    {
        Guard<fbl::Mutex> guard{&lock_};

        // add the new VMO as a child before we do anything, since its
        // dtor expects to find it in its parent's child list
        num_children = AddChildLocked(vmo.get());

        // check that we're not uncached in some way
        if (cache_policy_ != ARCH_MMU_FLAG_CACHED) {
            return ZX_ERR_BAD_STATE;
        }

        // set the offset with the parent
        status = vmo->SetParentOffsetLocked(offset);
        if (status != ZX_OK) {
            return status;
        }
        // The child shouldn't be able to see more pages if it grows or
        // pages which are beyond this VMO's current size.
        vmo->parent_limit_ = fbl::min(size, size_ - vmo->parent_offset_);

        if (copy_name) {
            vmo->name_ = name_;
        }
    }

    if (num_children == 1) {
        NotifyOneChild();
    }

    *child_vmo = ktl::move(vmo);

    return ZX_OK;
}

uint64_t VmObjectPaged::parent_user_id() const {
    canary_.Assert();
    // Don't hold both our lock and our parent's lock at the same time, because
    // it's probably the same lock.
    fbl::RefPtr<VmObject> parent;
    {
        Guard<fbl::Mutex> guard{&lock_};
        if (parent_ == nullptr) {
            return 0u;
        }
        parent = parent_;
    }
    return parent->user_id();
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
    printf("vmo %p/k%" PRIu64 " size %#" PRIx64 " offset %#" PRIx64
           " pages %zu ref %d parent %p/k%" PRIu64 "\n",
           this, user_id_, size_, parent_offset_, count,
           ref_count_debug(), parent_.get(), parent_id);

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
    return AllocatedPagesInRangeLocked(offset, len);
}

size_t VmObjectPaged::AllocatedPagesInRangeLocked(uint64_t offset, uint64_t len) const {
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

    if (offset >= size_) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    zx_status_t err = page_list_.AddPage(p, offset);
    if (err != ZX_OK) {
        return err;
    }

    // other mappings may have covered this offset into the vmo, so unmap those ranges
    RangeChangeUpdateLocked(offset, PAGE_SIZE);

    return ZX_OK;
}

vm_page_t* VmObjectPaged::FindInitialPageContentLocked(uint64_t offset, uint pf_flags,
                                                       VmObject** owner_out,
                                                       uint64_t* owner_offset_out) {
    DEBUG_ASSERT(page_list_.GetPage(offset) == nullptr);

    // Search up the clone chain for any committed pages. cur_offset is the offset
    // into cur we care about. The loop terminates either when that offset contains
    // a committed page or when that offset can't reach into the parent.
    vm_page_t* page = nullptr;
    VmObjectPaged* cur = this;
    uint64_t cur_offset = offset;
    while (!page && cur_offset < cur->parent_limit_) {
        // If there's no parent, then parent_limit_ is 0 and we'll never enter the loop
        DEBUG_ASSERT(cur->parent_);

        uint64_t parent_offset;
        bool overflowed = add_overflow(cur->parent_offset_, cur_offset, &parent_offset);
        ASSERT(!overflowed);
        if (parent_offset >= cur->parent_->size()) {
            // The offset is off the end of the parent, so cur is the VmObject
            // which will provide the page.
            break;
        }

        if (!cur->parent_->is_paged()) {
            uint parent_pf_flags = pf_flags & ~VMM_PF_FLAG_WRITE;
            auto status = cur->parent_->GetPageLocked(parent_offset, parent_pf_flags,
                                                      nullptr, nullptr, &page, nullptr);
            // The first if statement should ensure we never make an out-of-range query into a
            // physical VMO, and physical VMOs will always return a page for all valid offsets.
            DEBUG_ASSERT(status == ZX_OK);
            DEBUG_ASSERT(page != nullptr);

            *owner_out = cur->parent_.get();
            *owner_offset_out = parent_offset;
            return page;
        } else {
            cur = VmObjectPaged::AsVmObjectPaged(cur->parent_);
            cur_offset = parent_offset;
            page = cur->page_list_.GetPage(parent_offset);
        }
    }

    *owner_out = cur;
    *owner_offset_out = cur_offset;

    return page;
}

// Looks up the page at the requested offset, faulting it in if requested and necessary.  If
// this VMO has a parent and the requested page isn't found, the parent will be searched.
//
// |free_list|, if not NULL, is a list of allocated but unused vm_page_t that
// this function may allocate from.  This function will need at most one entry,
// and will not fail if |free_list| is a non-empty list, faulting in was requested,
// and offset is in range.
zx_status_t VmObjectPaged::GetPageLocked(uint64_t offset, uint pf_flags, list_node* free_list,
                                         PageRequest* page_request,
                                         vm_page_t** const page_out, paddr_t* const pa_out) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.lock().IsHeld());

    if (offset >= size_) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    vm_page_t* p;

    // see if we already have a page at that offset
    p = page_list_.GetPage(offset);
    if (p) {
        if (page_out) {
            *page_out = p;
        }
        if (pa_out) {
            *pa_out = p->paddr();
        }
        return ZX_OK;
    }

    __UNUSED char pf_string[5];
    LTRACEF("vmo %p, offset %#" PRIx64 ", pf_flags %#x (%s)\n", this, offset, pf_flags,
            vmm_pf_flags_to_string(pf_flags, pf_string));

    VmObject* page_owner;
    uint64_t owner_offset;
    if (!parent_) {
        // Avoid the function call in the common case.
        page_owner = this;
        owner_offset = offset;
    } else {
        p = FindInitialPageContentLocked(offset, pf_flags, &page_owner, &owner_offset);
    }

    if (!p) {
        // If we're not being asked to sw or hw fault in the page, return not found.
        if ((pf_flags & VMM_PF_FLAG_FAULT_MASK) == 0) {
            return ZX_ERR_NOT_FOUND;
        }

        // Since physical VMOs always provide pages for their full range, we should
        // never get here for physical VMOs.
        DEBUG_ASSERT(page_owner->is_paged());
        VmObjectPaged* typed_owner = static_cast<VmObjectPaged*>(page_owner);

        if (typed_owner->page_source_) {
            zx_status_t status =
                    typed_owner->page_source_->GetPage(owner_offset, page_request, &p, nullptr);
            // Pager page sources will never synchronously return a page.
            DEBUG_ASSERT(status != ZX_OK);

            if (typed_owner != this && status == ZX_ERR_NOT_FOUND) {
                // The default behavior of clones of detached pager VMOs fault in zero
                // pages instead of propagating the pager's fault.
                // TODO(stevensd): Add an arg to zx_vmo_create_child to optionally fault here.
                p = vm_get_zero_page();
            } else {
                return status;
            }
        } else {
            // If there's no page source, we're using an anonymous page. It's not
            // necessary to fault a writable page directly into the owning VMO.
            p = vm_get_zero_page();
        }
    }
    DEBUG_ASSERT(p);

    if ((pf_flags & VMM_PF_FLAG_WRITE) == 0) {
        // If we're read-only faulting, return the page so they can map or read from it directly.
        if (page_out) {
            *page_out = p;
        }
        if (pa_out) {
            *pa_out = p->paddr();
        }
        LTRACEF("read only faulting in page %p, pa %#" PRIxPTR " from parent\n", p, p->paddr());
        return ZX_OK;
    }

    // If we're write faulting, we need to allocate a wriable page into this VMO.
    vm_page_t* new_p = nullptr;
    paddr_t new_pa;
    if (free_list) {
        new_p = list_remove_head_type(free_list, vm_page, queue_node);
        if (new_p) {
            new_pa = new_p->paddr();
        }
    }
    if (!new_p) {
        pmm_alloc_page(pmm_alloc_flags_, &new_p, &new_pa);
        if (!new_p) {
            return ZX_ERR_NO_MEMORY;
        }
    }

    InitializeVmPage(new_p);

    void* dst = paddr_to_physmap(new_pa);
    DEBUG_ASSERT(dst);

    if (likely(p == vm_get_zero_page())) {
        // avoid pointless fetches by directly zeroing dst
        arch_zero_page(dst);

        // If ARM and not fully cached, clean/invalidate the page after zeroing it.
        // check doesn't need to be done in the other branch, since that branch is
        // only hit for clones and clones are always cached.
#if ARCH_ARM64
        if (cache_policy_ != ARCH_MMU_FLAG_CACHED) {
            arch_clean_invalidate_cache_range((addr_t) dst, PAGE_SIZE);
        }
#endif
    } else {
        // do a direct copy of the two pages
        const void* src = paddr_to_physmap(p->paddr());
        DEBUG_ASSERT(src);
        memcpy(dst, src, PAGE_SIZE);
    }

    // Add the new page and return it. This also is responsible for
    // unmapping this offset in any children.
    zx_status_t status = AddPageLocked(new_p, offset);
    DEBUG_ASSERT(status == ZX_OK);

    LTRACEF("faulted in page %p, pa %#" PRIxPTR " copied from %p\n", new_p, new_pa, p);

    if (page_out) {
        *page_out = new_p;
    }
    if (pa_out) {
        *pa_out = new_pa;
    }

    return ZX_OK;
}

zx_status_t VmObjectPaged::CommitRange(uint64_t offset, uint64_t len) {
    canary_.Assert();
    LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 "\n", offset, len);

    Guard<fbl::Mutex> guard{&lock_};

    // trim the size
    uint64_t new_len;
    if (!TrimRange(offset, len, size_, &new_len)) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    // was in range, just zero length
    if (new_len == 0) {
        return ZX_OK;
    }

    // compute a page aligned end to do our searches in to make sure we cover all the pages
    uint64_t end = ROUNDUP_PAGE_SIZE(offset + new_len);
    DEBUG_ASSERT(end > offset);
    offset = ROUNDDOWN(offset, PAGE_SIZE);

    fbl::RefPtr<PageSource> root_source = GetRootPageSourceLocked();

    // If this vmo has a direct page source, then the source will provide the backing memory. For
    // children that eventually depend on a page source, we skip preallocating memory to avoid
    // potentially overallocating pages if something else touches the vmo while we're blocked on the
    // request. Otherwise we optimize things by preallocating all the pages.
    list_node page_list;
    list_initialize(&page_list);
    if (root_source == nullptr) {
        // make a pass through the list to find out how many pages we need to allocate
        size_t count = (end - offset) / PAGE_SIZE;
        page_list_.ForEveryPageInRange(
            [&count](const auto p, auto off) {
                count--;
                return ZX_ERR_NEXT;
            },
            offset, end);

        if (count == 0) {
            return ZX_OK;
        }

        zx_status_t status = pmm_alloc_pages(count, pmm_alloc_flags_, &page_list);
        if (status != ZX_OK) {
            return status;
        }
    }

    auto list_cleanup = fbl::MakeAutoCall([&page_list]() {
        if (!list_is_empty(&page_list)) {
            pmm_free(&page_list);
        }
    });

    bool retry = false;
    PageRequest page_request(true);
    do {
        if (retry) {
            // If there was a page request that couldn't be fulfilled, we need wait on the
            // request and retry the commit. Note that when we retry the loop, offset is
            // updated past the portion of the vmo that we successfully commited.
            zx_status_t status = ZX_OK;
            guard.CallUnlocked([&page_request, &status]() mutable {
                status = page_request.Wait();
            });
            if (status != ZX_OK) {
                return status;
            }
            retry = false;

            // Re-run the range checks, since size_ could have changed while we were blocked. This
            // is not a failure, since the arguments were valid when the syscall was made. It's as
            // if the commit was successful but then the pages were thrown away.
            if (!TrimRange(offset, new_len, size_, &new_len)) {
                return ZX_OK;
            }

            if (new_len == 0) {
                return ZX_OK;
            }

            end = ROUNDUP_PAGE_SIZE(offset + new_len);
            DEBUG_ASSERT(end > offset);
            offset = ROUNDDOWN(offset, PAGE_SIZE);
        }

        // cur_offset tracks how far we've made page requests, even if they're not done
        uint64_t cur_offset = offset;
        // new_offset tracks how far we've successfully committed and is where we'll
        // restart from if we need to retry the commit
        uint64_t new_offset = offset;
        while (cur_offset < end) {
            // Don't commit if we already have this page
            vm_page_t* p = page_list_.GetPage(cur_offset);
            if (!p) {
                // Check if our parent has the page
                const uint flags = VMM_PF_FLAG_SW_FAULT | VMM_PF_FLAG_WRITE;
                zx_status_t res = GetPageLocked(cur_offset, flags, &page_list,
                                                &page_request, nullptr, nullptr);
                if (res == ZX_ERR_NEXT || res == ZX_ERR_SHOULD_WAIT) {
                    // In either case we'll need to wait on the request and retry, but if we get
                    // ZX_ERR_NEXT we keep faulting until we eventually see ZX_ERR_SHOULD_WAIT.
                    retry = true;
                    if (res == ZX_ERR_SHOULD_WAIT) {
                        break;
                    }
                } else if (res != ZX_OK) {
                    return res;
                }
            }

            cur_offset += PAGE_SIZE;
            if (!retry) {
                new_offset = offset;
            }
        }

        // Unmap all of the pages in the range we touched. This may end up unmapping non-present
        // ranges or unmapping things multiple times, but it's necessary to ensure that we unmap
        // everything that actually is present before anything else sees it.
        if (cur_offset - offset) {
            RangeChangeUpdateLocked(offset, cur_offset - offset);
        }

        if (retry && cur_offset == end) {
            zx_status_t res = root_source->FinalizeRequest(&page_request);
            if (res != ZX_ERR_SHOULD_WAIT) {
                return res;
            }
        }
        offset = new_offset;
    } while (retry);

    return ZX_OK;
}

zx_status_t VmObjectPaged::DecommitRange(uint64_t offset, uint64_t len) {
    canary_.Assert();
    LTRACEF("offset %#" PRIx64 ", len %#" PRIx64 "\n", offset, len);

    if (options_ & kContiguous) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    Guard<fbl::Mutex> guard{&lock_};

    // trim the size
    uint64_t new_len;
    if (!TrimRange(offset, len, size_, &new_len)) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    // was in range, just zero length
    if (new_len == 0) {
        return ZX_OK;
    }

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

    list_node_t list;
    list_initialize(&list);
    page_list_.RemovePages(start, end, &list);

    guard.Release();

    pmm_free(&list);

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
    if (unlikely(!InRange(offset, len, size_))) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    if (unlikely(len == 0)) {
        return ZX_OK;
    }

    const uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
    const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

    uint64_t pin_range_end = start_page_offset;
    zx_status_t status = page_list_.ForEveryPageAndGapInRange(
        [&pin_range_end](const auto p, uint64_t off) {
            DEBUG_ASSERT(p->state() == VM_PAGE_STATE_OBJECT);
            if (p->object.pin_count == VM_PAGE_OBJECT_MAX_PIN_COUNT) {
                return ZX_ERR_UNAVAILABLE;
            }

            p->object.pin_count++;
            pin_range_end = off + PAGE_SIZE;
            return ZX_ERR_NEXT;
        },
        [](uint64_t gap_start, uint64_t gap_end) {
            return ZX_ERR_NOT_FOUND;
        },
        start_page_offset, end_page_offset);

    if (status != ZX_OK) {
        UnpinLocked(start_page_offset, pin_range_end - start_page_offset);
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

    if (unlikely(len == 0)) {
        return;
    }

    const uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
    const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

    zx_status_t status = page_list_.ForEveryPageAndGapInRange(
        [](const auto p, uint64_t off) {
            DEBUG_ASSERT(p->state() == VM_PAGE_STATE_OBJECT);
            ASSERT(p->object.pin_count > 0);
            p->object.pin_count--;
            return ZX_ERR_NEXT;
        },
        [](uint64_t gap_start, uint64_t gap_end) {
            return ZX_ERR_NOT_FOUND;
        },
        start_page_offset, end_page_offset);
    ASSERT_MSG(status == ZX_OK, "Tried to unpin an uncommitted page");
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

zx_status_t VmObjectPaged::Resize(uint64_t s) {
    canary_.Assert();

    LTRACEF("vmo %p, size %" PRIu64 "\n", this, s);

    if (!(options_ & kResizable)) {
        return ZX_ERR_UNAVAILABLE;
    }

    // round up the size to the next page size boundary and make sure we dont wrap
    zx_status_t status = RoundSize(s, &s);
    if (status != ZX_OK) {
        return status;
    }

    Guard<fbl::Mutex> guard{&lock_};

    // make sure everything is aligned before we get started
    DEBUG_ASSERT(IS_PAGE_ALIGNED(size_));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(s));

    list_node_t free_list;
    list_initialize(&free_list);

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

        if (page_source_) {
            // Tell the page source that any non-resident pages that are now out-of-bounds
            // were supplied, to ensure that any reads of those pages get woken up.
            zx_status_t status = page_list_.ForEveryPageAndGapInRange(
                [](const auto p, uint64_t off) {
                    return ZX_ERR_NEXT;
                },
                [&](uint64_t gap_start, uint64_t gap_end) {
                    page_source_->OnPagesSupplied(gap_start, gap_end);
                    return ZX_ERR_NEXT;
                },
                start, end);
            DEBUG_ASSERT(status == ZX_OK);
        }

        parent_limit_ = fbl::min(parent_limit_, s);

        page_list_.RemovePages(start, end, &free_list);
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

    guard.Release();
    pmm_free(&free_list);

    return ZX_OK;
}

zx_status_t VmObjectPaged::SetParentOffsetLocked(uint64_t offset) {
    DEBUG_ASSERT(lock_.lock().IsHeld());

    // offset must be page aligned
    if (!IS_PAGE_ALIGNED(offset)) {
        return ZX_ERR_INVALID_ARGS;
    }

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
    if (cache_policy_ != ARCH_MMU_FLAG_CACHED) {
        return ZX_ERR_BAD_STATE;
    }

    // Test if in range. If we block on a page request, then it's possible for the
    // size to change. If that happens, then any out-of-bounds reads will be caught
    // by GetPageLocked.
    uint64_t end_offset;
    if (add_overflow(offset, len, &end_offset) || end_offset > size_) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    // Walk the list of pages and do the read/write. This is performed in
    // a loop to deal with blocking on asynchronous page requests.
    uint64_t src_offset = offset;
    size_t dest_offset = 0;
    PageRequest page_request;
    bool need_retry = false;
    do {
        if (need_retry) {
            // If we looped because of an asynchronous page request, block on it
            // outside the lock and then resume reading/writing.
            zx_status_t status;
            guard.CallUnlocked([&status, &page_request]() {
                status = page_request.Wait();
            });
            if (status != ZX_OK) {
                return status;
            }
            need_retry = false;
        }

        while (len > 0) {
            size_t page_offset = src_offset % PAGE_SIZE;
            size_t tocopy = fbl::min(PAGE_SIZE - page_offset, len);

            // fault in the page
            paddr_t pa;
            auto status = GetPageLocked(src_offset,
                                        VMM_PF_FLAG_SW_FAULT | (write ? VMM_PF_FLAG_WRITE : 0),
                                        nullptr, &page_request, nullptr, &pa);
            if (status == ZX_ERR_SHOULD_WAIT) {
                need_retry = true;
                break;
            } else if (status != ZX_OK) {
                return status;
            }

            // compute the kernel mapping of this page
            uint8_t* page_ptr = reinterpret_cast<uint8_t*>(paddr_to_physmap(pa));

            // call the copy routine
            auto err = copyfunc(page_ptr + page_offset, dest_offset, tocopy);
            if (err < 0) {
                return err;
            }

            src_offset += tocopy;
            dest_offset += tocopy;
            len -= tocopy;
        }
    } while (need_retry);

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

zx_status_t VmObjectPaged::Lookup(uint64_t offset, uint64_t len,
                                  vmo_lookup_fn_t lookup_fn, void* context) {
    canary_.Assert();
    if (unlikely(len == 0)) {
        return ZX_ERR_INVALID_ARGS;
    }

    Guard<fbl::Mutex> guard{&lock_};

    // verify that the range is within the object
    if (unlikely(!InRange(offset, len, size_))) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const uint64_t start_page_offset = ROUNDDOWN(offset, PAGE_SIZE);
    const uint64_t end_page_offset = ROUNDUP(offset + len, PAGE_SIZE);

    zx_status_t status = page_list_.ForEveryPageAndGapInRange(
        [lookup_fn, context, start_page_offset](const auto p, uint64_t off) {
            const size_t index = (off - start_page_offset) / PAGE_SIZE;
            paddr_t pa = p->paddr();
            zx_status_t status = lookup_fn(context, off, index, pa);
            if (status != ZX_OK) {
                if (unlikely(status == ZX_ERR_NEXT || status == ZX_ERR_STOP)) {
                    status = ZX_ERR_INTERNAL;
                }
                return status;
            }
            return ZX_ERR_NEXT;
        },
        [this, lookup_fn, context, start_page_offset](uint64_t gap_start, uint64_t gap_end) {
            // If some page was missing from our list, run the more expensive
            // GetPageLocked to see if our parent has it.
            for (uint64_t off = gap_start; off < gap_end; off += PAGE_SIZE) {

                paddr_t pa;
                zx_status_t status = this->GetPageLocked(off, 0, nullptr, nullptr, nullptr, &pa);
                if (status != ZX_OK) {
                    return ZX_ERR_NO_MEMORY;
                }
                const size_t index = (off - start_page_offset) / PAGE_SIZE;
                status = lookup_fn(context, off, index, pa);
                if (status != ZX_OK) {
                    if (unlikely(status == ZX_ERR_NEXT || status == ZX_ERR_STOP)) {
                        status = ZX_ERR_INTERNAL;
                    }
                    return status;
                }
            }
            return ZX_ERR_NEXT;
        },
        start_page_offset, end_page_offset);
    if (status != ZX_OK) {
        return status;
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

zx_status_t VmObjectPaged::TakePages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) {
    Guard<fbl::Mutex> src_guard{&lock_};
    uint64_t end;
    if (add_overflow(offset, len, &end) || size() < end) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    if (AnyPagesPinnedLocked(offset, len) || parent_ || page_source_) {
        return ZX_ERR_BAD_STATE;
    }

    // This is only used by the userpager API, which has significant restrictions on
    // what sorts of vmos are acceptable. If splice starts being used in more places,
    // then this restriction might need to be lifted.
    // TODO: Check that the region is locked once locking is implemented
    if (mapping_list_len_ || children_list_len_
            || AllocatedPagesInRangeLocked(offset , len) != (len / PAGE_SIZE)) {
        return ZX_ERR_BAD_STATE;
    }

    *pages = page_list_.TakePages(offset, len);

    return ZX_OK;
}

zx_status_t VmObjectPaged::SupplyPages(uint64_t offset, uint64_t len, VmPageSpliceList* pages) {
    Guard<fbl::Mutex> guard{&lock_};
    ASSERT(page_source_);

    uint64_t end;
    if (add_overflow(offset, len, &end) || size() < end) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    list_node free_list;
    list_initialize(&free_list);

    // [new_pages_start, new_pages_start + new_pages_len) tracks the current run of
    // consecutive new pages added to this vmo.
    uint64_t new_pages_start = offset;
    uint64_t new_pages_len = 0;
    zx_status_t status = ZX_OK;
    while (!pages->IsDone()) {
        vm_page* src_page = pages->Pop();
        status = AddPageLocked(src_page, offset);
        if (status == ZX_OK) {
            new_pages_len += PAGE_SIZE;
        } else {
            list_add_tail(&free_list, &src_page->queue_node);

            if (likely(status == ZX_ERR_ALREADY_EXISTS)) {
                status = ZX_OK;

                // We hit the end of a run of absent pages, so notify the pager source
                // of any new pages that were added and reset the tracking variables.
                if (new_pages_len) {
                    page_source_->OnPagesSupplied(new_pages_start, new_pages_len);
                }
                new_pages_start = offset + PAGE_SIZE;
                new_pages_len = 0;
            } else {
                break;
            }
        }
        offset += PAGE_SIZE;

        DEBUG_ASSERT(new_pages_start + new_pages_len <= end);
    }
    if (new_pages_len) {
        page_source_->OnPagesSupplied(new_pages_start, new_pages_len);
    }

    if (!list_is_empty(&free_list)) {
        pmm_free(&free_list);
    }

    return status;
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

    if (unlikely(len == 0)) {
        return ZX_ERR_INVALID_ARGS;
    }

    Guard<fbl::Mutex> guard{&lock_};

    if (unlikely(!InRange(start_offset, len, size_))) {
        return ZX_ERR_OUT_OF_RANGE;
    }

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
        auto status = GetPageLocked(op_start_offset, 0, nullptr, nullptr, nullptr, &pa);

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

uint32_t VmObjectPaged::GetMappingCachePolicy() const {
    Guard<fbl::Mutex> guard{&lock_};

    return cache_policy_;
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
    // 3) vmo has no children
    // 4) vmo is not a child
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
                      &offset_new, &len_new)) {
        return;
    }

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

fbl::RefPtr<PageSource> VmObjectPaged::GetRootPageSourceLocked() const {
    auto vm_object = this;
    while (vm_object->parent_) {
        vm_object = VmObjectPaged::AsVmObjectPaged(vm_object->parent_);
        if (!vm_object) {
            return nullptr;
        }
    }
    return vm_object->page_source_;
}
