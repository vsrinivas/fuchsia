// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "magma_util/refcounted.h"
#include "msd_platform_buffer.h"
#include <atomic>
#include <errno.h>
#include <limits.h> // PAGE_SIZE
#include <magenta/syscalls-ddk.h>
#include <vector>

class MagentaPlatformPage {
public:
    MagentaPlatformPage(void* virt_addr, uint64_t phys_addr)
        : virt_addr_(virt_addr), phys_addr_(phys_addr)
    {
        DASSERT((reinterpret_cast<uintptr_t>(virt_addr) & (PAGE_SIZE - 1)) == 0);
        DASSERT((phys_addr & (PAGE_SIZE - 1)) == 0);
    }

    void* virt_addr() { return virt_addr_; }
    uint64_t phys_addr() { return phys_addr_; }

private:
    void* virt_addr_;
    uint64_t phys_addr_;
};

class MagentaPlatformBuffer : public msd_platform_buffer, public magma::Refcounted {
public:
    MagentaPlatformBuffer(uint64_t size, void* vaddr, uint64_t paddr)
        : magma::Refcounted("MagentaPlatformBuffer"), size_(size), virt_addr_(vaddr),
          phys_addr_(paddr), handle_(++handle_count_)
    {
        DLOG("MagentaPlatformBuffer ctor size %lld handle 0x%x", size, handle_);
        magic_ = kMagic;
    }

    ~MagentaPlatformBuffer()
    {
        // TODO(MA-22) - free the allocation.
    }

    uint64_t size() { return size_; }

    uint32_t handle() { return handle_; }

    void* virt_addr() { return virt_addr_; }

    int PinPages();
    int UnpinPages();
    bool PinnedPageCount(uint32_t* count);

    MagentaPlatformPage* get_page(unsigned int index) { return pages_[index].get(); }

    static MagentaPlatformBuffer* cast(msd_platform_buffer* buffer)
    {
        DASSERT(buffer->magic_ == kMagic);
        return static_cast<MagentaPlatformBuffer*>(buffer);
    }

private:
    static uint32_t handle_count_;

    static const uint32_t kMagic = 0x62756666;

    uint64_t size_;
    void* virt_addr_;
    uint64_t phys_addr_;
    uint32_t handle_;
    std::atomic_int pin_count_{};
    std::vector<std::unique_ptr<MagentaPlatformPage>> pages_{};
};

uint32_t MagentaPlatformBuffer::handle_count_{};

int MagentaPlatformBuffer::PinPages()
{
    if (pin_count_++ == 0 && pages_.size() == 0) {
        uint64_t num_pages = size() / PAGE_SIZE;
        DASSERT(num_pages * PAGE_SIZE == size());
        DLOG("acquiring pages size %lld num_pages %zd", size(), num_pages);

        for (unsigned int i = 0; i < num_pages; i++) {
            void* vaddr = reinterpret_cast<uint8_t*>(virt_addr_) + i * PAGE_SIZE;
            uint64_t paddr = phys_addr_ + i * PAGE_SIZE;
            auto page = new (std::nothrow) MagentaPlatformPage(vaddr, paddr);
            if (!page) {
                DLOG("Failed allocating page %u", i);
                pages_.clear();
                return DRET(-ENOMEM);
            }
            pages_.push_back(std::unique_ptr<MagentaPlatformPage>(page));
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
        pages_.clear();
    }
    return 0;
}

bool MagentaPlatformBuffer::PinnedPageCount(uint32_t* count)
{
    if (pin_count_ > 0) {
        *count = pages_.size();
        return true;
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////////

int msd_platform_buffer_alloc(struct msd_platform_buffer** buffer_out, uint64_t size,
                              uint64_t* size_out, uint32_t* handle_out)
{
    size = magma::round_up_64(size, PAGE_SIZE);
    if (size == 0)
        return DRET(-EINVAL);

    void* vaddr;
    mx_paddr_t paddr;
    mx_status_t status = mx_alloc_device_memory(size, &paddr, &vaddr);
    if (status < 0) {
        DLOG("failed to allocate device memory size %lld: %d", size, status);
        return DRET(-ENOMEM);
    }

    DLOG("allocated object size %lld vaddr %p paddr %p", size, vaddr, (void*)paddr);
    auto buffer = new MagentaPlatformBuffer(size, vaddr, paddr);

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
    *addr_out = MagentaPlatformBuffer::cast(buffer)->virt_addr();
    return 0;
}

int32_t msd_platform_buffer_unmap_cpu(struct msd_platform_buffer* buffer)
{
    // Nothing to do.
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
    auto page = MagentaPlatformBuffer::cast(buffer)->get_page(page_index);
    if (!page)
        return DRET(-EINVAL);
    *addr_out = page->virt_addr();
    return 0;
}

int32_t msd_platform_buffer_unmap_page_cpu(struct msd_platform_buffer* buffer, uint32_t page_index)
{
    auto page = MagentaPlatformBuffer::cast(buffer)->get_page(page_index);
    if (!page)
        return DRET(-EINVAL);
    return 0;
}

int32_t msd_platform_buffer_map_page_bus(struct msd_platform_buffer* buffer, uint32_t page_index,
                                         uint64_t* addr_out)
{
    auto page = MagentaPlatformBuffer::cast(buffer)->get_page(page_index);
    if (!page)
        return DRET(-EINVAL);
    *addr_out = page->phys_addr();
    return 0;
}

int32_t msd_platform_buffer_unmap_page_bus(struct msd_platform_buffer* buffer, uint32_t page_index)
{
    auto page = MagentaPlatformBuffer::cast(buffer)->get_page(page_index);
    if (!page)
        return DRET(-EINVAL);
    return 0;
}
