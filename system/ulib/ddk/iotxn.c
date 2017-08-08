// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <ddk/iotxn.h>
#include <ddk/device.h>
#include <magenta/assert.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <sys/param.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <threads.h>

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

// Warn if free list length exceeds a multiple of FREE_LIST_MONITOR_LIMIT
#define FREE_LIST_MONITOR_LIMIT 100

#define IOTXN_PFLAG_CONTIGUOUS (1 << 0)   // the vmo is contiguous
#define IOTXN_PFLAG_ALLOC      (1 << 1)   // the vmo is allocated by us
#define IOTXN_PFLAG_PHYSMAP    (1 << 2)   // we performed physmap() on this vmo and allocated memory
#define IOTXN_PFLAG_MMAP       (1 << 3)   // we performed mmap() on this vmo
#define IOTXN_PFLAG_FREE       (1 << 4)   // this txn has been released
#define IOTXN_PFLAG_QUEUED     (1 << 5)   // transaction has been queued and not yet released

#define IOTXN_STATE_MASK       (IOTXN_PFLAG_FREE | IOTXN_PFLAG_QUEUED)

static list_node_t free_list = LIST_INITIAL_VALUE(free_list);
static mtx_t free_list_mutex = MTX_INIT;
#if FREE_LIST_MONITOR_LIMIT
static size_t free_list_length = 0;
static size_t free_list_monitor_warned = 0;
#endif

// This assert will fail if we attempt to access the buffer of a cloned txn after it has been completed
#define ASSERT_BUFFER_VALID(priv) MX_DEBUG_ASSERT(!(priv->flags & IOTXN_FLAG_DEAD))

static uint32_t alloc_flags_to_pflags(uint32_t alloc_flags) {
    if (alloc_flags & IOTXN_ALLOC_CONTIGUOUS) {
        return IOTXN_PFLAG_CONTIGUOUS;
    }
    return 0;
}

static bool do_free_phys(uint32_t pflags) {
    // only free phys if we called physmap and allocated memory
    return (pflags & IOTXN_PFLAG_PHYSMAP);
}

static iotxn_t* find_in_free_list(uint32_t pflags, uint64_t data_size) {
    bool found = false;
    iotxn_t* txn = NULL;
    //xprintf("find_in_free_list pflags 0x%x data_size 0x%" PRIx64 "\n", pflags, data_size);
    mtx_lock(&free_list_mutex);
    list_for_every_entry (&free_list, txn, iotxn_t, node) {
        // txn->pflags has IOTXN_ALLOC_CONTIGUOUS set if the txn has a contiguous VMO we allocated,
        // or zero otherwise. And the pflags passed into this function is either zero or
        // IOTXN_ALLOC_CONTIGUOUS. So here we mask txn->pflags with IOTXN_ALLOC_CONTIGUOUS
        // to compare just this bit and not get confused by IOTXN_PFLAG_FREE or other flags.
        if ((txn->vmo_length == data_size) && (((txn->pflags & IOTXN_ALLOC_CONTIGUOUS) == pflags) || data_size == 0)) {
            found = true;
            break;
        }
    }
    if (found) {
        txn->pflags &= ~IOTXN_PFLAG_FREE;
        list_delete(&txn->node);
#if FREE_LIST_MONITOR_LIMIT
        free_list_length--;
#endif
    }
    mtx_unlock(&free_list_mutex);
    //xprintf("find_in_free_list found %d txn %p\n", found, txn);
    return found ? txn : NULL;
}

// return the iotxn into the free list
static void iotxn_release_free_list(iotxn_t* txn) {
    mx_handle_t vmo_handle = txn->vmo_handle;
    uint64_t vmo_offset = txn->vmo_offset;
    uint64_t vmo_length = txn->vmo_length;
    void* virt = txn->virt;
    mx_paddr_t* phys = txn->phys;
    uint64_t phys_count = txn->phys_count;
    uint32_t pflags = txn->pflags;
    mx_paddr_t phys_inline[3];
    memcpy(phys_inline, txn->phys_inline, sizeof(txn->phys_inline));

    memset(txn, 0, sizeof(iotxn_t));

    if (pflags & IOTXN_PFLAG_ALLOC) {
        // if we allocated the vmo, keep it around
        txn->vmo_handle = vmo_handle;
        txn->vmo_offset = vmo_offset;
        txn->vmo_length = vmo_length;
        txn->virt = virt;
        txn->phys = phys;
        txn->phys_count = phys_count;
        txn->pflags = pflags;
        memcpy(txn->phys_inline, phys_inline, sizeof(txn->phys_inline));
    } else {
        if (do_free_phys(pflags)) {
            if (phys != NULL) {
                free(phys);
            }
        }
        if (pflags & IOTXN_PFLAG_MMAP) {
            // only unmap if we called mmap()
            if (virt) {
                mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)virt, vmo_length);
            }
        }
    }

    txn->pflags |= IOTXN_PFLAG_FREE;
    txn->release_cb = iotxn_release_free_list;

    mtx_lock(&free_list_mutex);
    list_add_head(&free_list, &txn->node);
#if FREE_LIST_MONITOR_LIMIT
    free_list_length++;
    if (free_list_length % FREE_LIST_MONITOR_LIMIT == 0
        && free_list_length > free_list_monitor_warned) {
        printf("WARNING: iotxn free_list_length is %zu\n", free_list_length);
        free_list_monitor_warned = free_list_length;
    }
#endif
    mtx_unlock(&free_list_mutex);

    xprintf("iotxn_release_free_list released txn %p\n", txn);
}

// free the iotxn
static void iotxn_release_free(iotxn_t* txn) {
    if (do_free_phys(txn->pflags)) {
        if (txn->phys != NULL) {
            free(txn->phys);
        }
    }
    if (txn->pflags & IOTXN_PFLAG_MMAP) {
        if (txn->virt) {
            mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)txn->virt, txn->vmo_length);
        }
    }
    if (txn->pflags & IOTXN_PFLAG_ALLOC) {
        mx_handle_close(txn->vmo_handle);
    }
    free(txn);
}

// releases data for a statically allocated iotxn
static void iotxn_release_static(iotxn_t* txn) {
    uint32_t pflags = txn->pflags;

    if (do_free_phys(txn->pflags)) {
        // only free the scatter list if we called physmap()
        if (txn->phys != NULL) {
            free(txn->phys);
            txn->phys = NULL;
            txn->phys_count = 0;
        }
    }
    if (pflags & IOTXN_PFLAG_MMAP) {
        // only unmap if we called mmap()
        if (txn->virt) {
            mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)txn->virt, txn->vmo_length);
            txn->virt = NULL;
        }
    }
}

void iotxn_complete(iotxn_t* txn, mx_status_t status, mx_off_t actual) {
    xprintf("iotxn_complete txn %p\n", txn);

    MX_DEBUG_ASSERT((txn->pflags & IOTXN_STATE_MASK) == IOTXN_PFLAG_QUEUED);
    txn->pflags &= ~IOTXN_PFLAG_QUEUED;

    txn->actual = actual;
    txn->status = status;
    if (txn->complete_cb) {
        txn->complete_cb(txn, txn->cookie);
    }
}

ssize_t iotxn_copyfrom(iotxn_t* txn, void* data, size_t length, size_t offset) {
    length = MIN(txn->vmo_length - offset, length);
    size_t actual;
    mx_status_t status = mx_vmo_read(txn->vmo_handle, data, txn->vmo_offset + offset, length, &actual);
    xprintf("iotxn_copyfrom: txn %p vmo_offset 0x%" PRIx64 " offset 0x%zx length 0x%zx actual 0x%zx status %d\n", txn, txn->vmo_offset, offset, length, actual, status);
    return (status == MX_OK) ? (ssize_t)actual : status;
}

ssize_t iotxn_copyto(iotxn_t* txn, const void* data, size_t length, size_t offset) {
    length = MIN(txn->vmo_length - offset, length);
    size_t actual;
    mx_status_t status = mx_vmo_write(txn->vmo_handle, data, txn->vmo_offset + offset, length, &actual);
    xprintf("iotxn_copyto: txn %p vmo_offset 0x%" PRIx64 " offset 0x%zx length 0x%zx actual 0x%zx status %d\n", txn, txn->vmo_offset, offset, length, actual, status);
    return (status == MX_OK) ? (ssize_t)actual : status;
}

static mx_status_t iotxn_physmap_contiguous(iotxn_t* txn) {
    txn->phys = txn->phys_inline;

    // for contiguous buffers, commit the whole range but just map the first
    // page
    uint64_t page_offset = ROUNDDOWN(txn->vmo_offset, PAGE_SIZE);
    mx_status_t status = mx_vmo_op_range(txn->vmo_handle, MX_VMO_OP_COMMIT, page_offset, txn->vmo_length, NULL, 0);
    if (status != MX_OK) {
        goto fail;
    }

    status = mx_vmo_op_range(txn->vmo_handle, MX_VMO_OP_LOOKUP, page_offset, PAGE_SIZE, txn->phys, sizeof(mx_paddr_t));
    if (status != MX_OK) {
        goto fail;
    }

    txn->phys_count = 1;
    return MX_OK;
fail:
    txn->phys = NULL;
    return status;
}

static mx_status_t iotxn_physmap_paged(iotxn_t* txn) {
    // MX_VMO_OP_LOOKUP returns whole pages, so take into account unaligned vmo
    // offset and length when calculating the amount of pages returned
    uint64_t page_offset = ROUNDDOWN(txn->vmo_offset, PAGE_SIZE);
    uint64_t page_length = txn->vmo_length + (txn->vmo_offset - page_offset);
    uint64_t pages = ROUNDUP(page_length, PAGE_SIZE) / PAGE_SIZE;

    bool use_inline = pages <= 3;
    mx_paddr_t* paddrs = use_inline ? txn->phys_inline : malloc(sizeof(mx_paddr_t) * pages);
    if (paddrs == NULL) {
        xprintf("iotxn_physmap_paged: out of memory\n");
        return MX_ERR_NO_MEMORY;
    }

    // commit pages and lookup physical addresses
    // assume that commited pages will never be auto-decommitted
    mx_status_t status = mx_vmo_op_range(txn->vmo_handle, MX_VMO_OP_COMMIT, txn->vmo_offset, txn->vmo_length, NULL, 0);
    if (status != MX_OK) {
        xprintf("iotxn_physmap_paged: error %d in commit\n", status);
        if (!use_inline) {
            free(paddrs);
        }
        return status;
    }

    status = mx_vmo_op_range(txn->vmo_handle, MX_VMO_OP_LOOKUP, page_offset, page_length, paddrs, sizeof(mx_paddr_t) * pages);
    if (status != MX_OK) {
        xprintf("iotxn_physmap_paged: error %d in lookup\n", status);
        if (!use_inline) {
            free(paddrs);
        }
        return status;
    }

    if (!use_inline) {
        txn->pflags |= IOTXN_PFLAG_PHYSMAP;
    }
    txn->phys = paddrs;
    txn->phys_count = pages;
    return MX_OK;
}

mx_status_t iotxn_physmap(iotxn_t* txn) {
    if (txn->phys_count > 0) {
        return MX_OK;
    }
    if (txn->vmo_length == 0) {
        return MX_ERR_INVALID_ARGS;
    }
    mx_status_t status;
    if (txn->pflags & IOTXN_PFLAG_CONTIGUOUS) {
        status = iotxn_physmap_contiguous(txn);
    } else {
        status = iotxn_physmap_paged(txn);
    }
    return status;
}

mx_status_t iotxn_mmap(iotxn_t* txn, void** data) {
    xprintf("iotxn_mmap: txn %p\n", txn);
    if (txn->virt) {
        *data = txn->virt;
        return MX_OK;
    }
    mx_status_t status = mx_vmar_map(mx_vmar_root_self(), 0, txn->vmo_handle, txn->vmo_offset, txn->vmo_length, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, (uintptr_t*)(&txn->virt));
    if (status == MX_OK) {
        txn->pflags |= IOTXN_PFLAG_MMAP;
        *data = txn->virt;
    }
    return status;
}

mx_status_t iotxn_clone(iotxn_t* txn, iotxn_t** out) {
    xprintf("iotxn_clone txn %p\n", txn);
    iotxn_t* clone = NULL;
    if (*out != NULL) {
        clone = *out;
    } else {
        clone = find_in_free_list(0, 0);
        if (clone == NULL) {
            clone = calloc(1, sizeof(iotxn_t));
            if (clone == NULL) {
                return MX_ERR_NO_MEMORY;
            }
        }
    }

    memcpy(clone, txn, sizeof(iotxn_t));
    // the only relevant pflag for a clone is the contiguous bit
    clone->pflags = txn->pflags & IOTXN_PFLAG_CONTIGUOUS;
    clone->complete_cb = NULL;
    // clones are always freelisted on release
    clone->release_cb = iotxn_release_free_list;

    *out = clone;
    return MX_OK;
}

mx_status_t iotxn_clone_partial(iotxn_t* txn, uint64_t vmo_offset, mx_off_t length, iotxn_t** out) {
    xprintf("iotxn_clone_partial txn %p\n", txn);
    if (vmo_offset < txn->vmo_offset) {
        return MX_ERR_INVALID_ARGS;
    }
    if (length > txn->length) {
        return MX_ERR_INVALID_ARGS;
    }
    if ((vmo_offset - txn->vmo_offset) > (length - txn->length)) {
        return MX_ERR_INVALID_ARGS;
    }

    mx_status_t status = iotxn_clone(txn, out);
    if (status != MX_OK) {
        return status;
    }

    iotxn_t* clone = *out;
    uint64_t offd = vmo_offset - txn->vmo_offset;
    clone->vmo_offset = vmo_offset;
    clone->vmo_length -= offd;
    clone->length = length;
    if (clone->virt) {
        clone->virt += offd;
    }
    if (clone->phys) {
        uint64_t page_offset = ROUNDDOWN(txn->vmo_offset, PAGE_SIZE);
        uint64_t new_page_offset = ROUNDDOWN(clone->vmo_offset, PAGE_SIZE);
        if (page_offset != new_page_offset) {
            if (txn->pflags & IOTXN_PFLAG_CONTIGUOUS) {
                clone->phys = clone->phys_inline;
                clone->phys[0] = txn->phys[0] + new_page_offset - page_offset;
            } else {
                uint64_t pages = (new_page_offset - page_offset) / PAGE_SIZE;
                if (pages >= clone->phys_count) {
                    iotxn_release(clone);
                    return MX_ERR_INVALID_ARGS;
                }
                clone->phys += pages;
                clone->phys_count -= pages;
                MX_DEBUG_ASSERT(clone->phys_count > 0);
            }
        }
    }
    MX_DEBUG_ASSERT(clone->release_cb == iotxn_release_free_list);
    return MX_OK;
}

void iotxn_release(iotxn_t* txn) {
    // should not release a queued transaction
    MX_DEBUG_ASSERT((txn->pflags & IOTXN_STATE_MASK) == 0);

    if (txn->release_cb) {
        txn->release_cb(txn);
    }
}

void iotxn_cacheop(iotxn_t* txn, uint32_t op, size_t offset, size_t length) {
    // Bail out if the syscall has nothing to do.
    if (length == 0 || txn->vmo_length == 0)
        return;

    mx_vmo_op_range(txn->vmo_handle, op, txn->vmo_offset + offset, length, NULL, 0);
}

mx_status_t iotxn_alloc(iotxn_t** out, uint32_t alloc_flags, uint64_t data_size) {
    //xprintf("iotxn_alloc: alloc_flags 0x%x data_size 0x%" PRIx64 "\n", alloc_flags, data_size);

    // look in free list first for a iotxn with data_size
    iotxn_t* txn = find_in_free_list(alloc_flags_to_pflags(alloc_flags), data_size);
    if (txn != NULL) {
        //xprintf("iotxn_alloc: found iotxn with size 0x%" PRIx64 " in free list\n", data_size);
        goto out;
    }

    // didn't find one that fits, allocate a new one
    txn = calloc(1, sizeof(iotxn_t));
    if (!txn) {
        return MX_ERR_NO_MEMORY;
    }
    if (data_size > 0) {
        mx_status_t status;
        if (alloc_flags & IOTXN_ALLOC_CONTIGUOUS) {
            status = mx_vmo_create_contiguous(get_root_resource(), data_size, 0, &txn->vmo_handle);
            txn->pflags |= IOTXN_PFLAG_CONTIGUOUS;
        } else {
            status = mx_vmo_create(data_size, 0, &txn->vmo_handle);
        }
        mx_object_set_property(txn->vmo_handle, MX_PROP_NAME, "iotxn", 5);
        if (status != MX_OK) {
            xprintf("iotxn_alloc: error %d in mx_vmo_create, flags 0x%x\n", status, alloc_flags);
            free(txn);
            return status;
        }
        txn->vmo_offset = 0;
        txn->vmo_length = data_size;
        txn->pflags |= IOTXN_PFLAG_ALLOC;
    }

out:
    MX_DEBUG_ASSERT(txn != NULL);
    MX_DEBUG_ASSERT(!(txn->pflags & IOTXN_PFLAG_FREE));
    if (alloc_flags & IOTXN_ALLOC_POOL) {
        txn->release_cb = iotxn_release_free_list;
    } else {
        txn->release_cb = iotxn_release_free;
    }
    *out = txn;
    return MX_OK;
}

void iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    // don't assert not queued here, since iotxns are allowed to be requeued
    txn->pflags |= IOTXN_PFLAG_QUEUED;

    // This can only fail if iotxn_queue() is not implemented by the
    // device, in which case we fall back to calling the read or write op
    if (device_iotxn_queue(dev, txn) != MX_OK) {
        mx_status_t status;
        size_t actual = 0;
        void* buf;
        iotxn_mmap(txn, &buf);
        if (txn->opcode == IOTXN_OP_READ) {
            status = device_read(dev, buf, txn->length, txn->offset, &actual);
        } else if (txn->opcode == IOTXN_OP_WRITE) {
            status = device_write(dev, buf, txn->length, txn->offset, &actual);
        } else {
            status = MX_ERR_NOT_SUPPORTED;
        }
        iotxn_complete(txn, status, actual);
    }
}

void iotxn_init(iotxn_t* txn, mx_handle_t vmo_handle, uint64_t vmo_offset, uint64_t length) {
    memset(txn, 0, sizeof(*txn));
    txn->vmo_handle = vmo_handle;
    txn->vmo_offset = vmo_offset;
    txn->vmo_length = length;
    txn->length = length;
    txn->release_cb = iotxn_release_static;
}

mx_status_t iotxn_alloc_vmo(iotxn_t** out, uint32_t alloc_flags, mx_handle_t vmo_handle,
                            uint64_t vmo_offset, uint64_t length) {
    iotxn_t* txn;
    mx_status_t status = iotxn_alloc(&txn, alloc_flags, 0);
    if (status != MX_OK) {
        return status;
    }
    txn->vmo_handle = vmo_handle;
    txn->vmo_offset = vmo_offset;
    txn->vmo_length = length;
    txn->length = length;
    *out = txn;
    return MX_OK;
}

void iotxn_phys_iter_init(iotxn_phys_iter_t* iter, iotxn_t* txn, size_t max_length) {
    iter->txn = txn;
    iter->offset = 0;
    MX_DEBUG_ASSERT(max_length % PAGE_SIZE == 0);
    if (max_length == 0) {
        max_length = UINT64_MAX;
    }
    iter->max_length = max_length;
    // iter->page is index of page containing txn->vmo_offset,
    // and iter->last_page is index of page containing txn->vmo_offset + txn->length
    iter->page = 0;
    if (txn->length > 0) {
        size_t align_adjust = txn->vmo_offset & (PAGE_SIZE - 1);
        iter->last_page = (txn->length + align_adjust - 1) / PAGE_SIZE;
    } else {
        iter->last_page = 0;
    }
}

size_t iotxn_phys_iter_next(iotxn_phys_iter_t* iter, mx_paddr_t* out_paddr) {
    iotxn_t* txn = iter->txn;
    mx_off_t offset = iter->offset;
    size_t max_length = iter->max_length;
    size_t length = txn->length;
    if (offset >= length) {
        return 0;
    }
    size_t remaining = length - offset;
    mx_paddr_t* phys_addrs = txn->phys;
    size_t align_adjust = txn->vmo_offset & (PAGE_SIZE - 1);
    mx_paddr_t phys = phys_addrs[iter->page];
    size_t return_length = 0;

    if (txn->phys_count == 1) {
        // simple contiguous case
        *out_paddr = phys_addrs[0] + offset + align_adjust;
        return_length = remaining;
        if (return_length > max_length) {
            // end on a page boundary
            return_length = max_length - align_adjust;
        }
        iter->offset += return_length;
        return return_length;
    }

    if (offset == 0 && align_adjust > 0) {
        // if vmo_offset is unaligned we need to adjust out_paddr, accumulate partial page length
        // in return_length and skip to next page.
        // we will make sure the range ends on a page boundary so we don't need to worry about
        // alignment for subsequent iterations.
        *out_paddr = phys + align_adjust;
        return_length = PAGE_SIZE - align_adjust;
        remaining -= return_length;
        iter->page = 1;

        if (iter->page > iter->last_page || phys + PAGE_SIZE != phys_addrs[iter->page]) {
            iter->offset += return_length;
            return return_length;
        }
        phys = phys_addrs[iter->page];
    } else {
        *out_paddr = phys;
    }

    // below is more complicated case where we need to watch for discontinuities
    // in the physical address space.

    // loop through physical addresses looking for discontinuities
    while (remaining > 0 && iter->page <= iter->last_page) {
        const size_t increment = MIN(PAGE_SIZE, remaining);
        if (return_length + increment > max_length) {
            break;
        }
        return_length += increment;
        remaining -= increment;
        iter->page++;

        if (iter->page > iter->last_page) {
            break;
        }

        mx_paddr_t next = phys_addrs[iter->page];
        if (phys + PAGE_SIZE != next) {
            break;
        }
        phys = next;
    }

    if (return_length > max_length) {
        return_length = max_length;
    }
    iter->offset += return_length;
    return return_length;
}
