// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "magma_util/refcounted.h"
#include "msd_platform_buffer.h"
#include <atomic>
#include <ddk/driver.h>
#include <errno.h>
#include <limits.h> // PAGE_SIZE
#include <magenta/syscalls.h>

struct MagentaPlatformPage {
    uint64_t phys_addr;
    void* virt_addr;
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
        UnpinPages();
        mx_handle_close(handle_);
    }

    uint64_t size() { return size_; }

    uint32_t num_pages() { return size_ / PAGE_SIZE; }

    uint32_t handle() { return handle_; }

    void* MapCpu();
    void UnmapCpu();

    int PinPages();
    int UnpinPages();
    bool PinnedPageCount(uint32_t* count);

    void* MapPageCpu(uint32_t page_index);
    void UnmapPageCpu(uint32_t page_index);
    uint64_t MapPageBus(uint32_t page_index);

    static MagentaPlatformBuffer* cast(msd_platform_buffer* buffer)
    {
        DASSERT(buffer->magic_ == kMagic);
        return static_cast<MagentaPlatformBuffer*>(buffer);
    }

private:
    static const uint32_t kMagic = 0x62756666;

    mx_handle_t handle_;
    uint64_t size_;
    void* virt_addr_{};
    std::atomic_int pin_count_{};
    MagentaPlatformPage* pages_{};
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

int MagentaPlatformBuffer::PinPages()
{
    if (pin_count_++ == 0 && !pages_) {
        DASSERT(num_pages() * PAGE_SIZE == size());
        DLOG("acquiring pages size %lld num_pages %zd", size(), num_pages());

        mx_status_t status = mx_vmo_op_range(handle_, MX_VMO_OP_COMMIT, 0, size(), nullptr, 0);
        if (status != NO_ERROR) {
            DLOG("failed to commit vmo");
            return DRET(-ENOMEM);
        }

        mx_paddr_t paddr[num_pages()];

        status = mx_vmo_op_range(handle_, MX_VMO_OP_LOOKUP, 0, size(), paddr, sizeof(paddr));
        if (status != NO_ERROR) {
            DLOG("failed to lookup vmo");
            return DRET(-ENOMEM);
        }

        pages_ = new MagentaPlatformPage[num_pages()];

        for (unsigned int i = 0; i < num_pages(); i++) {
            pages_[i].phys_addr = paddr[i];
            pages_[i].virt_addr = nullptr;
        }
    }
    return 0;
}

int MagentaPlatformBuffer::UnpinPages()
{
    if (pin_count_ < 1)
        return DRET(-EINVAL);

    if (--pin_count_ == 0) {
        DLOG("pin_count 0, freeing pages");
        DASSERT(pages_);

        for (uint32_t page_index = 0; page_index < num_pages(); page_index++) {
            if (pages_[page_index].virt_addr)
                UnmapPageCpu(page_index);
        }

        delete[] pages_;
        pages_ = nullptr;

        mx_status_t status = mx_vmo_op_range(handle_, MX_VMO_OP_DECOMMIT, 0, size(), nullptr, 0);
        if (status != NO_ERROR && status != ERR_NOT_SUPPORTED)
            DLOG("failed to decommit pages: %d", status);
    }
    return 0;
}

bool MagentaPlatformBuffer::PinnedPageCount(uint32_t* count)
{
    if (pin_count_ > 0) {
        *count = size() / PAGE_SIZE;
        return true;
    }
    return false;
}

void* MagentaPlatformBuffer::MapPageCpu(uint32_t page_index)
{
    if (!pages_)
        return DRETP(nullptr, "pages not pinned");

    if (page_index >= num_pages())
        return DRETP(nullptr, "page_index out of range");

    if (pages_[page_index].virt_addr)
        return pages_[page_index].virt_addr;

    uintptr_t ptr;
    mx_status_t status =
        mx_process_map_vm(mx_process_self(), handle_, page_index * PAGE_SIZE, PAGE_SIZE, &ptr,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
    if (status != NO_ERROR)
        return DRETP(nullptr, "page map failed");

    return (pages_[page_index].virt_addr = reinterpret_cast<void*>(ptr));
}

void MagentaPlatformBuffer::UnmapPageCpu(uint32_t page_index)
{
    if (!pages_ || (page_index >= num_pages()) || !pages_[page_index].virt_addr)
        return;

    mx_status_t status = mx_process_unmap_vm(
        mx_process_self(), reinterpret_cast<uintptr_t>(pages_[page_index].virt_addr), 0);
    if (status != NO_ERROR)
        DLOG("failed to unmap vmo page %d virt_addr %p", page_index, pages_[page_index].virt_addr);

    pages_[page_index].virt_addr = nullptr;
}

uint64_t MagentaPlatformBuffer::MapPageBus(uint32_t page_index)
{
    if (!pages_) {
        DLOG("pages not pinned");
        return 0;
    }

    if (page_index >= num_pages()) {
        DLOG("page_index out of range");
        return 0;
    }

    return pages_[page_index].phys_addr;
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

void msd_platform_buffer_incref(struct msd_platform_buffer* buffer)
{
    MagentaPlatformBuffer::cast(buffer)->Incref();
}

void msd_platform_buffer_decref(struct msd_platform_buffer* buffer)
{
    MagentaPlatformBuffer::cast(buffer)->Decref();
}

uint32_t msd_platform_buffer_getref(struct msd_platform_buffer* buffer)
{
    return MagentaPlatformBuffer::cast(buffer)->Getref();
}

int32_t msd_platform_buffer_get_size(struct msd_platform_buffer* buffer, uint64_t* size_out)
{
    *size_out = MagentaPlatformBuffer::cast(buffer)->size();
    return 0;
}

int32_t msd_platform_buffer_get_handle(struct msd_platform_buffer* buffer, uint32_t* handle_out)
{
    *handle_out = MagentaPlatformBuffer::cast(buffer)->handle();
    return 0;
}

int32_t msd_platform_buffer_map_cpu(struct msd_platform_buffer* buffer, void** addr_out)
{
    *addr_out = MagentaPlatformBuffer::cast(buffer)->MapCpu();
    return 0;
}

int32_t msd_platform_buffer_unmap_cpu(struct msd_platform_buffer* buffer)
{
    MagentaPlatformBuffer::cast(buffer)->UnmapCpu();
    return 0;
}

int32_t msd_platform_buffer_pin_pages(struct msd_platform_buffer* buffer)
{
    int ret = MagentaPlatformBuffer::cast(buffer)->PinPages();
    return DRET(ret);
}

int32_t msd_platform_buffer_unpin_pages(struct msd_platform_buffer* buffer)
{
    int ret = MagentaPlatformBuffer::cast(buffer)->UnpinPages();
    return DRET(ret);
}

int32_t msd_platform_buffer_pinned_page_count(struct msd_platform_buffer* buffer,
                                              uint32_t* num_pages_out)
{
    if (!MagentaPlatformBuffer::cast(buffer)->PinnedPageCount(num_pages_out))
        return DRET(-EINVAL);
    return 0;
}

int32_t msd_platform_buffer_map_page_cpu(struct msd_platform_buffer* buffer, uint32_t page_index,
                                         void** addr_out)
{
    void* virt_addr = MagentaPlatformBuffer::cast(buffer)->MapPageCpu(page_index);
    if (!virt_addr)
        return DRET(-EINVAL);
    *addr_out = virt_addr;
    return 0;
}

int32_t msd_platform_buffer_unmap_page_cpu(struct msd_platform_buffer* buffer, uint32_t page_index)
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
