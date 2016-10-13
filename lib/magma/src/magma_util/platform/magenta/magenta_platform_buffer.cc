// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "magma_util/refcounted.h"
#include "msd_platform_buffer.h"
#include <ddk/driver.h>
#include <errno.h>
#include <limits.h> // PAGE_SIZE
#include <magenta/syscalls.h>
#include <map>

struct PinnedPage {
    uint64_t phys_addr;
    uint32_t pin_count;
};

class MagentaPlatformBuffer : public msd_platform_buffer, public magma::Refcounted {
public:
    MagentaPlatformBuffer(mx_handle_t handle, uint64_t size)
        : magma::Refcounted("MagentaPlatformBuffer"), handle_(handle), size_(size)
    {
        DLOG("MagentaPlatformBuffer ctor size %lld handle 0x%x", size, handle_);
        magic_ = kMagic;
    }

    ~MagentaPlatformBuffer()
    {
        UnmapCpu();
        ReleasePages();
        mx_handle_close(handle_);
    }

    uint64_t size() { return size_; }

    uint32_t num_pages() { return size_ / PAGE_SIZE; }

    uint32_t handle() { return handle_; }

    void* MapCpu();
    void UnmapCpu();

    int PinPages(uint32_t start_page_index, uint32_t page_count);
    int UnpinPages(uint32_t start_page_index, uint32_t page_count);

    void* MapPageCpu(uint32_t page_index);
    void UnmapPageCpu(uint32_t page_index);

    // Requires the given page to be pinned.
    uint64_t MapPageBus(uint32_t page_index);

    static MagentaPlatformBuffer* cast(msd_platform_buffer* buffer)
    {
        DASSERT(buffer->magic_ == kMagic);
        return static_cast<MagentaPlatformBuffer*>(buffer);
    }

private:
    void ReleasePages();

    static const uint32_t kMagic = 0x62756666;

    mx_handle_t handle_;
    uint64_t size_;
    void* virt_addr_{};
    std::map<uint32_t, PinnedPage> pinned_pages_;
    std::map<uint32_t, void*> mapped_pages_;
};

void* MagentaPlatformBuffer::MapCpu()
{
    if (virt_addr_)
        return virt_addr_;

    uintptr_t ptr;
    mx_status_t status = mx_process_map_vm(mx_process_self(), handle_, 0, size(), &ptr,
                                           MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
    if (status != NO_ERROR)
        return DRETP(nullptr, "failed to map vmo");

    virt_addr_ = reinterpret_cast<void*>(ptr);
    DLOG("mapped vmo got %p", virt_addr_);

    return virt_addr_;
}

void MagentaPlatformBuffer::UnmapCpu()
{
    if (virt_addr_) {
        mx_status_t status =
            mx_process_unmap_vm(mx_process_self(), reinterpret_cast<uintptr_t>(virt_addr_), 0);
        if (status != NO_ERROR)
            DLOG("failed to unmap vmo: %d", status);

        virt_addr_ = nullptr;
    }
}

int MagentaPlatformBuffer::PinPages(uint32_t start_page_index, uint32_t page_count)
{
    if (!page_count)
        return 0;

    if ((start_page_index + page_count) * PAGE_SIZE > size())
        return DRET_MSG(-EINVAL, "offset + length greater than buffer size");

    mx_status_t status = mx_vmo_op_range(handle_, MX_VMO_OP_COMMIT, start_page_index * PAGE_SIZE, page_count * PAGE_SIZE, nullptr, 0);
    if (status != NO_ERROR)
        return DRET_MSG(-ENOMEM, "failed to commit vmo");

    mx_paddr_t paddr[page_count];

    status = mx_vmo_op_range(handle_, MX_VMO_OP_LOOKUP, start_page_index * PAGE_SIZE, page_count * PAGE_SIZE, paddr, sizeof(paddr));
    if (status != NO_ERROR)
        return DRET_MSG(-ENOMEM, "failed to lookup vmo");

    for (uint32_t i = 0; i < page_count; i++) {
        auto iter = pinned_pages_.find(start_page_index + i);
        if (iter == pinned_pages_.end()) {
            pinned_pages_[start_page_index + i] = PinnedPage{paddr[i], 1};
        } else {
            auto& page = iter->second;
            DASSERT(page.phys_addr == paddr[i]);
            page.pin_count++;
        }
    }

    return 0;
}

int MagentaPlatformBuffer::UnpinPages(uint32_t start_page_index, uint32_t page_count)
{
    if (!page_count)
        return 0;

    if ((start_page_index + page_count) * PAGE_SIZE > size())
        return DRET_MSG(-EINVAL, "offset + length greater than buffer size");

    uint32_t pages_to_unpin = 0;

    // Validate all pages in the range are pinned.
    for (uint32_t i = 0; i < page_count; i++) {
        auto iter = pinned_pages_.find(start_page_index + i);
        if (iter == pinned_pages_.end())
            return DRET_MSG(-EINVAL, "page_index %d not pinned", start_page_index + i);

        auto& page = iter->second;
        DASSERT(page.pin_count > 0);
        if (page.pin_count == 1)
            pages_to_unpin++;
    }

    DLOG("pages_to_unpin %u page_count %u", pages_to_unpin, page_count);

    int ret = 0;

    if (pages_to_unpin == page_count) {
        // Unpin the entire range.
        for (uint32_t i = 0; i < page_count; i++) {
            pinned_pages_.erase(start_page_index + i);
        }

        mx_status_t status =
            mx_vmo_op_range(handle_, MX_VMO_OP_DECOMMIT, start_page_index * PAGE_SIZE, page_count * PAGE_SIZE, nullptr, 0);
        if (status != NO_ERROR && status != ERR_NOT_SUPPORTED) {
            DLOG("failed to decommit full range: %d", status);
            ret = -EINVAL;
        }

    } else {
        // Page by page
        auto iter = pinned_pages_.find(start_page_index);

        for (uint32_t i = 0; i < page_count; i++) {
            DASSERT(iter != pinned_pages_.end());

            auto& page = iter->second;
            if (--page.pin_count == 0) {
                mx_status_t status = mx_vmo_op_range(handle_, MX_VMO_OP_DECOMMIT,
                                                     (start_page_index + i) * PAGE_SIZE, PAGE_SIZE, nullptr, 0);
                if (status != NO_ERROR && status != ERR_NOT_SUPPORTED) {
                    DLOG("failed to decommit page_index %d: %d", start_page_index + i, status);
                    ret = -EINVAL;
                }
                auto erase_iter = iter;
                iter++;
                pinned_pages_.erase(erase_iter);
            } else {
                iter++;
            }
        }
    }

    return DRET(ret);
}

void MagentaPlatformBuffer::ReleasePages()
{
    if (pinned_pages_.size()) {
        // Have some pinned pages, decommit full range.
        mx_status_t status = mx_vmo_op_range(handle_, MX_VMO_OP_DECOMMIT, 0, size(), nullptr, 0);
        if (status != NO_ERROR && status != ERR_NOT_SUPPORTED)
            DLOG("failed to decommit pages: %d", status);
    }

    for (auto& pair : mapped_pages_) {
        UnmapPageCpu(pair.first);
    }
}

void* MagentaPlatformBuffer::MapPageCpu(uint32_t page_index)
{
    auto iter = mapped_pages_.find(page_index);
    if (iter != mapped_pages_.end())
        return iter->second;

    uintptr_t ptr;
    mx_status_t status =
        mx_process_map_vm(mx_process_self(), handle_, page_index * PAGE_SIZE, PAGE_SIZE, &ptr,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
    if (status != NO_ERROR)
        return DRETP(nullptr, "page map failed");

    return (mapped_pages_[page_index] = reinterpret_cast<void*>(ptr));
}

void MagentaPlatformBuffer::UnmapPageCpu(uint32_t page_index)
{
    auto iter = mapped_pages_.find(page_index);
    if (iter == mapped_pages_.end()) {
        DLOG("page_index %u not pinned", page_index);
        return;
    }

    uintptr_t addr = reinterpret_cast<uintptr_t>(iter->second);
    mapped_pages_.erase(iter);

    mx_status_t status = mx_process_unmap_vm(mx_process_self(), addr, 0);
    if (status != NO_ERROR)
        DLOG("failed to unmap vmo page %d", page_index);
}

uint64_t MagentaPlatformBuffer::MapPageBus(uint32_t page_index)
{
    auto iter = pinned_pages_.find(page_index);
    if (iter == pinned_pages_.end()) {
        DLOG("page_index %u not pinned", page_index);
        return 0;
    }

    auto& page = iter->second;
    DASSERT(page.pin_count > 0);

    return page.phys_addr;
}

//////////////////////////////////////////////////////////////////////////////

int32_t msd_platform_buffer_alloc(struct msd_platform_buffer** buffer_out, uint64_t size,
                                  uint64_t* size_out, uint32_t* handle_out)
{
    size = magma::round_up(size, PAGE_SIZE);
    if (size == 0)
        return DRET(-EINVAL);

    mx_handle_t vmo_handle = mx_vmo_create(size);
    if (vmo_handle == MX_HANDLE_INVALID) {
        DLOG("failed to allocate vmo size %lld: %d", size, vmo_handle);
        return DRET(-ENOMEM);
    }

    DLOG("allocated vmo size %lld handle 0x%x", size, vmo_handle);
    auto buffer = new MagentaPlatformBuffer(vmo_handle, size);

    *buffer_out = buffer;
    *size_out = buffer->size();
    *handle_out = buffer->handle();

    return 0;
}

void msd_platform_buffer_incref(msd_platform_buffer* buffer)
{
    MagentaPlatformBuffer::cast(buffer)->Incref();
}

void msd_platform_buffer_decref(msd_platform_buffer* buffer)
{
    MagentaPlatformBuffer::cast(buffer)->Decref();
}

uint32_t msd_platform_buffer_getref(msd_platform_buffer* buffer)
{
    return MagentaPlatformBuffer::cast(buffer)->Getref();
}

int32_t msd_platform_buffer_get_size(msd_platform_buffer* buffer, uint64_t* size_out)
{
    *size_out = MagentaPlatformBuffer::cast(buffer)->size();
    return 0;
}

int32_t msd_platform_buffer_get_handle(msd_platform_buffer* buffer, uint32_t* handle_out)
{
    *handle_out = MagentaPlatformBuffer::cast(buffer)->handle();
    return 0;
}

int32_t msd_platform_buffer_map_cpu(msd_platform_buffer* buffer, void** addr_out)
{
    *addr_out = MagentaPlatformBuffer::cast(buffer)->MapCpu();
    return 0;
}

int32_t msd_platform_buffer_unmap_cpu(msd_platform_buffer* buffer)
{
    MagentaPlatformBuffer::cast(buffer)->UnmapCpu();
    return 0;
}

int32_t msd_platform_buffer_pin_pages(msd_platform_buffer* buffer, uint32_t start_page_index,
                                      uint32_t page_count)
{
    int ret = MagentaPlatformBuffer::cast(buffer)->PinPages(start_page_index, page_count);
    return DRET(ret);
}

int32_t msd_platform_buffer_unpin_pages(msd_platform_buffer* buffer, uint32_t start_page_index,
                                      uint32_t page_count)
{
    int ret = MagentaPlatformBuffer::cast(buffer)->UnpinPages(start_page_index, page_count);
    return DRET(ret);
}

int32_t msd_platform_buffer_map_page_cpu(msd_platform_buffer* buffer, uint32_t page_index,
                                         void** addr_out)
{
    void* virt_addr = MagentaPlatformBuffer::cast(buffer)->MapPageCpu(page_index);
    if (!virt_addr)
        return DRET(-EINVAL);
    *addr_out = virt_addr;
    return 0;
}

int32_t msd_platform_buffer_unmap_page_cpu(msd_platform_buffer* buffer, uint32_t page_index)
{
    MagentaPlatformBuffer::cast(buffer)->UnmapPageCpu(page_index);
    return 0;
}

int32_t msd_platform_buffer_map_page_bus(struct msd_platform_buffer* buffer, uint32_t page_index,
                                         uint64_t* addr_out)
{
    uint64_t phys_addr = MagentaPlatformBuffer::cast(buffer)->MapPageBus(page_index);
    if (!phys_addr)
        return DRET(-EINVAL);
    *addr_out = phys_addr;
    return 0;
}

int32_t msd_platform_buffer_unmap_page_bus(struct msd_platform_buffer* buffer, uint32_t page_index)
{
    return 0;
}
