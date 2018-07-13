// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_buffer.h"

#include "platform_trace.h"
#include "zircon_platform_handle.h"
#include <lib/fdio/io.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <limits.h> // PAGE_SIZE
#include <map>
#include <vector>

namespace magma {

// static
uint64_t PlatformBuffer::MinimumMappableAddress()
{
    zx_info_vmar_t root_info;
    zx::vmar::root_self()->get_info(ZX_INFO_VMAR, &root_info, sizeof(root_info), nullptr, nullptr);
    return root_info.base;
}

bool ZirconPlatformBuffer::MapAtCpuAddr(uint64_t addr)
{
    if (!magma::is_page_aligned(addr))
        return DRETF(false, "addr %lx isn't page aligned", addr);
    if (map_count_ > 0)
        return DRETF(false, "buffer is already mapped");

    uint64_t minimum_mappable = MinimumMappableAddress();
    if (addr < minimum_mappable)
        return DRETF(false, "addr %lx below vmar base %lx", addr, minimum_mappable);

    uint64_t child_addr;
    zx_status_t status =
        zx::vmar::root_self()->allocate(addr - minimum_mappable, size(),
                                       ZX_VM_FLAG_CAN_MAP_READ | ZX_VM_FLAG_CAN_MAP_WRITE |
                                           ZX_VM_FLAG_CAN_MAP_SPECIFIC | ZX_VM_FLAG_SPECIFIC,
                                       &vmar_, &child_addr);
    if (status != ZX_OK)
        return DRETF(false, "Failed to create vmar, status %d", status);
    DASSERT(child_addr == addr);

    uintptr_t ptr;
    status = vmar_.map(0, vmo_, 0, size(),
                       ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_SPECIFIC, &ptr);
    if (status != ZX_OK)
        return DRETF(false, "failed to map vmo");
    DASSERT(ptr == addr);
    virt_addr_ = reinterpret_cast<void*>(ptr);

    map_count_++;

    DLOG("mapped vmo %p got %p, map_count_ = %u", this, virt_addr_, map_count_);
    return true;
}

bool ZirconPlatformBuffer::MapCpu(void** addr_out, uint64_t alignment)
{
    if (!magma::is_page_aligned(alignment))
        return DRETF(false, "alignment 0x%lx isn't page aligned", alignment);
    if (alignment && !magma::is_pow2(alignment))
        return DRETF(false, "alignment 0x%lx isn't power of 2", alignment);
    if (map_count_ == 0) {
        DASSERT(!virt_addr_);
        uintptr_t ptr;
        uintptr_t child_addr;
        // If alignment is needed, allocate a vmar that's large enough so that
        // the buffer will fit at an aligned address inside it.
        uintptr_t vmar_size = alignment ? size() + alignment : size();
        zx_status_t status = zx::vmar::root_self()->allocate(
            0, vmar_size,
            ZX_VM_FLAG_CAN_MAP_READ | ZX_VM_FLAG_CAN_MAP_WRITE | ZX_VM_FLAG_CAN_MAP_SPECIFIC,
            &vmar_, &child_addr);
        if (status != ZX_OK)
            return DRETF(false, "failed to make vmar");
        uintptr_t offset = alignment ? magma::round_up(child_addr, alignment) - child_addr : 0;
        status =
            vmar_.map(offset, vmo_, 0, size(),
                      ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_SPECIFIC, &ptr);
        if (status != ZX_OK)
            return DRETF(false, "failed to map vmo");

        virt_addr_ = reinterpret_cast<void*>(ptr);
    }

    DASSERT(!alignment || (reinterpret_cast<uintptr_t>(virt_addr_) & (alignment - 1)) == 0);

    *addr_out = virt_addr_;
    map_count_++;

    DLOG("mapped vmo %p got %p, map_count_ = %u", this, virt_addr_, map_count_);

    return true;
}

bool ZirconPlatformBuffer::UnmapCpu()
{
    DLOG("UnmapCpu vmo %p, map_count_ %u", this, map_count_);
    if (map_count_) {
        map_count_--;
        if (map_count_ == 0) {
            DLOG("map_count 0 unmapping vmo %p", this);
            zx_status_t status = vmar_unmap();
            if (status != ZX_OK)
                DRETF(false, "failed to unmap vmo: %d", status);
        }
        return true;
    }
    return DRETF(false, "attempting to unmap buffer that isnt mapped");
}

bool ZirconPlatformBuffer::CommitPages(uint32_t start_page_index, uint32_t page_count) const
{
    TRACE_DURATION("magma", "CommitPages");
    if (!page_count)
        return true;

    if ((start_page_index + page_count) * PAGE_SIZE > size())
        return DRETF(false, "offset + length greater than buffer size");

    zx_status_t status = vmo_.op_range(ZX_VMO_OP_COMMIT, start_page_index * PAGE_SIZE,
                                       page_count * PAGE_SIZE, nullptr, 0);

    if (status == ZX_ERR_NO_MEMORY)
        return DRETF(false,
                     "Kernel returned ZX_ERR_NO_MEMORY when attempting to commit %u vmo "
                     "pages (%u bytes).\nThis means the system has run out of physical memory and "
                     "things will now start going very badly.\nPlease stop using so much "
                     "physical memory or download more RAM at www.downloadmoreram.com :)",
                     page_count, PAGE_SIZE * page_count);
    else if (status != ZX_OK)
        return DRETF(false, "failed to commit vmo pages: %d", status);

    return true;
}

bool ZirconPlatformBuffer::CleanCache(uint64_t offset, uint64_t length, bool invalidate)
{
#if defined(__aarch64__)
    if (map_count_) {
        uint32_t op = ZX_CACHE_FLUSH_DATA;
        if (invalidate)
            op |= ZX_CACHE_FLUSH_INVALIDATE;
        if (offset + length > size())
            return DRETF(false, "size too large for buffer");
        zx_status_t status = zx_cache_flush(static_cast<uint8_t*>(virt_addr_) + offset, length, op);
        if (status != ZX_OK)
            return DRETF(false, "failed to clean cache: %d", status);
        return true;
    }
#endif

    uint32_t op = invalidate ? ZX_VMO_OP_CACHE_CLEAN_INVALIDATE : ZX_VMO_OP_CACHE_CLEAN;
    zx_status_t status = vmo_.op_range(op, offset, length, nullptr, 0);
    if (status != ZX_OK)
        return DRETF(false, "failed to clean cache: %d", status);
    return true;
}

bool ZirconPlatformBuffer::SetCachePolicy(magma_cache_policy_t cache_policy)
{
    uint32_t zx_cache_policy;
    switch (cache_policy) {
        case MAGMA_CACHE_POLICY_CACHED:
            zx_cache_policy = ZX_CACHE_POLICY_CACHED;
            break;

        case MAGMA_CACHE_POLICY_WRITE_COMBINING:
            zx_cache_policy = ZX_CACHE_POLICY_WRITE_COMBINING;
            break;

       case MAGMA_CACHE_POLICY_UNCACHED:
            zx_cache_policy = ZX_CACHE_POLICY_UNCACHED;
            break;

        default:
            return DRETF(false, "Invalid cache policy %d", cache_policy);
    }

    zx_status_t status = zx_vmo_set_cache_policy(vmo_.get(), zx_cache_policy);
    return DRETF(status == ZX_OK, "zx_vmo_set_cache_policy failed with status %d", status);
}

std::unique_ptr<PlatformBuffer> PlatformBuffer::Create(uint64_t size, const char* name)
{
    size = magma::round_up(size, PAGE_SIZE);
    if (size == 0)
        return DRETP(nullptr, "attempting to allocate 0 sized buffer");

    zx::vmo vmo;
    zx_status_t status = zx::vmo::create(size, 0, &vmo);
    if (status != ZX_OK)
        return DRETP(nullptr, "failed to allocate vmo size %" PRId64 ": %d", size, status);
    vmo.set_property(ZX_PROP_NAME, name, strlen(name));

    DLOG("allocated vmo size %ld handle 0x%x", size, vmo.get());
    return std::unique_ptr<PlatformBuffer>(new ZirconPlatformBuffer(std::move(vmo), size));
}

std::unique_ptr<PlatformBuffer> PlatformBuffer::Import(uint32_t handle)
{
    uint64_t size;
    // presumably this will fail if handle is invalid or not a vmo handle, so we perform no
    // additional error checking
    zx::vmo vmo(handle);
    auto status = vmo.get_size(&size);

    if (status != ZX_OK)
        return DRETP(nullptr, "zx_vmo_get_size failed");

    if (!magma::is_page_aligned(size))
        return DRETP(nullptr, "attempting to import vmo with invalid size");

    return std::unique_ptr<PlatformBuffer>(new ZirconPlatformBuffer(std::move(vmo), size));
}

} // namespace magma
