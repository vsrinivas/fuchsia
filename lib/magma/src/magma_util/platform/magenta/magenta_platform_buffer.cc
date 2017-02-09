// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_buffer.h"
#include <ddk/driver.h>
#include <limits.h>  // PAGE_SIZE
#include <magenta/syscalls/object.h>
#include <map>
#include <mx/vmar.h>
#include <mx/vmo.h>
#include <vector>

namespace magma {

class PinCountArray {
public:
    using pin_count_t = uint8_t;

    static constexpr uint32_t kPinCounts = PAGE_SIZE / sizeof(pin_count_t);

    PinCountArray() : count_(kPinCounts) {}

    uint32_t pin_count(uint32_t index)
    {
        DASSERT(index < count_.size());
        return count_[index];
    }

    void incr(uint32_t index)
    {
        DASSERT(index < count_.size());
        total_count_++;
        DASSERT(count_[index] < static_cast<pin_count_t>(~0));
        ++count_[index];
    }

    // If pin count is positive, decrements the pin count and returns the new pin count.
    // Otherwise returns -1.
    int32_t decr(uint32_t index)
    {
        DASSERT(index < count_.size());
        if (count_[index] == 0)
            return -1;
        DASSERT(total_count_ > 0);
        --total_count_;
        return --count_[index];
    }

    uint32_t total_count() { return total_count_; }

private:
    uint32_t total_count_{};
    std::vector<pin_count_t> count_;
};

class PinCountSparseArray {
public:
    static std::unique_ptr<PinCountSparseArray> Create(uint32_t page_count)
    {
        uint32_t array_size = page_count / PinCountArray::kPinCounts;
        if (page_count % PinCountArray::kPinCounts)
            array_size++;
        return std::unique_ptr<PinCountSparseArray>(new PinCountSparseArray(array_size));
    }

    uint32_t total_pin_count() { return total_pin_count_; }

    uint32_t pin_count(uint32_t page_index)
    {
        uint32_t array_index = page_index / PinCountArray::kPinCounts;
        uint32_t array_offset = page_index - PinCountArray::kPinCounts * array_index;

        auto& array = sparse_array_[array_index];
        if (!array)
            return 0;

        return array->pin_count(array_offset);
    }

    void incr(uint32_t page_index)
    {
        uint32_t array_index = page_index / PinCountArray::kPinCounts;
        uint32_t array_offset = page_index - PinCountArray::kPinCounts * array_index;

        if (!sparse_array_[array_index]) {
            sparse_array_[array_index] = std::unique_ptr<PinCountArray>(new PinCountArray());
        }

        sparse_array_[array_index]->incr(array_offset);

        ++total_pin_count_;
    }

    int32_t decr(uint32_t page_index)
    {
        uint32_t array_index = page_index / PinCountArray::kPinCounts;
        uint32_t array_offset = page_index - PinCountArray::kPinCounts * array_index;

        auto& array = sparse_array_[array_index];
        if (!array)
            return DRETF(false, "page not pinned");

        int32_t ret = array->decr(array_offset);
        if (ret < 0)
            return DRET_MSG(ret, "page not pinned");

        --total_pin_count_;

        if (array->total_count() == 0)
            array.reset();

        return ret;
    }

private:
    PinCountSparseArray(uint32_t array_size) : sparse_array_(array_size) {}

    std::vector<std::unique_ptr<PinCountArray>> sparse_array_;
    uint32_t total_pin_count_{};
};

class MagentaPlatformBuffer : public PlatformBuffer {
public:
    MagentaPlatformBuffer(mx::vmo vmo, uint64_t size) : vmo_(std::move(vmo)), size_(size)
    {
        DLOG("MagentaPlatformBuffer ctor size %ld vmo 0x%x", size, vmo_.get());

        DASSERT(magma::is_page_aligned(size));
        pin_count_array_ = PinCountSparseArray::Create(size / PAGE_SIZE);

        bool success = PlatformBuffer::IdFromHandle(vmo_.get(), &koid_);
        DASSERT(success);
    }

    ~MagentaPlatformBuffer() override
    {
        if (map_count_ > 0)
            UnmapCpu();
        ReleasePages();
    }

    // PlatformBuffer implementation
    uint64_t size() override { return size_; }

    uint64_t id() override { return koid_; }

    bool duplicate_handle(uint32_t* handle_out) override
    {
        mx::vmo duplicate;
        mx_status_t status = vmo_.duplicate(MX_RIGHT_SAME_RIGHTS, &duplicate);
        if (status < 0)
            return DRETF(false, "mx_handle_duplicate failed");
        *handle_out = duplicate.release();
        return true;
    }

    // PlatformBuffer implementation
    bool MapCpu(void** addr_out) override;
    bool UnmapCpu() override;

    bool PinPages(uint32_t start_page_index, uint32_t page_count) override;
    bool UnpinPages(uint32_t start_page_index, uint32_t page_count) override;

    bool MapPageCpu(uint32_t page_index, void** addr_out) override;
    bool UnmapPageCpu(uint32_t page_index) override;

    bool MapPageRangeBus(uint32_t start_page_index, uint32_t page_count,
                         uint64_t addr_out[]) override;
    bool UnmapPageRangeBus(uint32_t start_page_index, uint32_t page_count) override;

    uint32_t num_pages() { return size_ / PAGE_SIZE; }

private:
    void ReleasePages();

    mx::vmo vmo_;
    uint64_t size_;
    uint64_t koid_;
    void* virt_addr_{};
    uint32_t map_count_ = 0;
    std::unique_ptr<PinCountSparseArray> pin_count_array_;
    std::map<uint32_t, void*> mapped_pages_;
};

bool MagentaPlatformBuffer::MapCpu(void** addr_out)
{
    if (map_count_ == 0) {
        DASSERT(!virt_addr_);
        uintptr_t ptr;
        mx_status_t status = mx::vmar::root_self().map(
            0, vmo_, 0, size(), MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &ptr);
        if (status != NO_ERROR)
            return DRETF(false, "failed to map vmo");

        virt_addr_ = reinterpret_cast<void*>(ptr);
    }

    *addr_out = virt_addr_;
    map_count_++;

    DLOG("mapped vmo %p got %p, map_count_ = %u", this, virt_addr_, map_count_);

    return true;
}

bool MagentaPlatformBuffer::UnmapCpu()
{
    DLOG("UnmapCpu vmo %p, map_count_ %u", this, map_count_);
    if (map_count_) {
        map_count_--;
        if (map_count_ == 0) {
            DLOG("map_count 0 unmapping vmo %p", this);
            mx_status_t status = mx::vmar::root_self().unmap(
                    reinterpret_cast<uintptr_t>(virt_addr_), size());
            virt_addr_ = nullptr;
            if (status != NO_ERROR)
                DRETF(false, "failed to unmap vmo: %d", status);
        }
        return true;
    }
    return DRETF(false, "attempting to unmap buffer that isnt mapped\n");
}

bool MagentaPlatformBuffer::PinPages(uint32_t start_page_index, uint32_t page_count)
{
    if (!page_count)
        return true;

    if ((start_page_index + page_count) * PAGE_SIZE > size())
        return DRETF(false, "offset + length greater than buffer size");

    mx_status_t status = vmo_.op_range(MX_VMO_OP_COMMIT, start_page_index * PAGE_SIZE,
                                       page_count * PAGE_SIZE, nullptr, 0);
    if (status != NO_ERROR)
        return DRETF(false, "failed to commit vmo pages: 0x%x", status);

    status = vmo_.op_range(MX_VMO_OP_LOCK, start_page_index * PAGE_SIZE, page_count * PAGE_SIZE,
                           nullptr, 0);
    if (status != NO_ERROR && status != ERR_NOT_SUPPORTED)
        return DRETF(false, "failed to lock vmo pages: 0x%x", status);

    for (uint32_t i = 0; i < page_count; i++) {
        pin_count_array_->incr(start_page_index + i);
    }

    return true;
}

bool MagentaPlatformBuffer::UnpinPages(uint32_t start_page_index, uint32_t page_count)
{
    if (!page_count)
        return true;

    if ((start_page_index + page_count) * PAGE_SIZE > size())
        return DRETF(false, "offset + length greater than buffer size");

    uint32_t pages_to_unpin = 0;

    for (uint32_t i = 0; i < page_count; i++) {
        uint32_t pin_count = pin_count_array_->pin_count(start_page_index + i);

        if (pin_count == 0)
            return DRETF(false, "page not pinned");

        if (pin_count == 1)
            pages_to_unpin++;
    }

    DLOG("pages_to_unpin %u page_count %u", pages_to_unpin, page_count);

    if (pages_to_unpin == page_count) {
        for (uint32_t i = 0; i < page_count; i++) {
            pin_count_array_->decr(start_page_index + i);
        }

        // Unlock the entire range.
        mx_status_t status = vmo_.op_range(MX_VMO_OP_UNLOCK, start_page_index * PAGE_SIZE,
                                           page_count * PAGE_SIZE, nullptr, 0);
        if (status != NO_ERROR && status != ERR_NOT_SUPPORTED) {
            return DRETF(false, "failed to unlock full range: %d", status);
        }

    } else {
        // Unlock page by page
        for (uint32_t page_index = start_page_index; page_index < start_page_index + page_count;
             page_index++) {
            if (pin_count_array_->decr(page_index) == 0) {
                mx_status_t status =
                    vmo_.op_range(MX_VMO_OP_UNLOCK, page_index * PAGE_SIZE, PAGE_SIZE, nullptr, 0);
                if (status != NO_ERROR && status != ERR_NOT_SUPPORTED) {
                    return DRETF(false, "failed to unlock page_index %u: %u", page_index, status);
                }
            }
        }
    }

    return true;
}

void MagentaPlatformBuffer::ReleasePages()
{
    if (pin_count_array_->total_pin_count()) {
        // Still have some pinned pages, unlock.
        mx_status_t status = vmo_.op_range(MX_VMO_OP_UNLOCK, 0, size(), nullptr, 0);
        if (status != NO_ERROR && status != ERR_NOT_SUPPORTED)
            DLOG("failed to unlock pages: %d", status);
    }

    for (auto& pair : mapped_pages_) {
        UnmapPageCpu(pair.first);
    }
}

bool MagentaPlatformBuffer::MapPageCpu(uint32_t page_index, void** addr_out)
{
    auto iter = mapped_pages_.find(page_index);
    if (iter != mapped_pages_.end()) {
        *addr_out = iter->second;
        return true;
    }

    uintptr_t ptr;
    mx_status_t status = mx::vmar::root_self().map(0, vmo_, page_index * PAGE_SIZE, PAGE_SIZE,
                                                   MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                                                   &ptr);
    if (status != NO_ERROR)
        return DRETF(false, "page map failed");

    *addr_out = reinterpret_cast<void*>(ptr);
    mapped_pages_.insert(std::make_pair(page_index, *addr_out));
    return true;
}

bool MagentaPlatformBuffer::UnmapPageCpu(uint32_t page_index)
{
    auto iter = mapped_pages_.find(page_index);
    if (iter == mapped_pages_.end()) {
        return DRETF(false, "page_index %u not mapped", page_index);
    }

    uintptr_t addr = reinterpret_cast<uintptr_t>(iter->second);
    mapped_pages_.erase(iter);

    mx_status_t status = mx::vmar::root_self().unmap(addr, PAGE_SIZE);
    if (status != NO_ERROR)
        return DRETF(false, "failed to unmap vmo page %d", page_index);

    return true;
}

bool MagentaPlatformBuffer::MapPageRangeBus(uint32_t start_page_index, uint32_t page_count,
                                            uint64_t addr_out[])
{
    static_assert(sizeof(mx_paddr_t) == sizeof(uint64_t), "unexpected sizeof(mx_paddr_t)");

    for (uint32_t i = start_page_index; i < start_page_index + page_count; i++) {
        if (pin_count_array_->pin_count(i) == 0)
            return DRETF(false, "zero pin_count for page %u", i);
    }

    mx_status_t status =
        vmo_.op_range(MX_VMO_OP_LOOKUP, start_page_index * PAGE_SIZE, page_count * PAGE_SIZE,
                      addr_out, page_count * sizeof(addr_out[0]));
    if (status != NO_ERROR)
        return DRETF(false, "failed to lookup vmo");

    return true;
}

bool MagentaPlatformBuffer::UnmapPageRangeBus(uint32_t start_page_index, uint32_t page_count)
{
    return true;
}

bool PlatformBuffer::IdFromHandle(uint32_t handle, uint64_t* id_out)
{
    mx_info_handle_basic_t info;
    mx_status_t status = mx_object_get_info(handle, MX_INFO_HANDLE_BASIC, &info,
                                            sizeof(info), nullptr, nullptr);
    if (status != NO_ERROR)
        return DRETF(false, "mx_object_get_info failed");

    *id_out = info.koid;
    return true;
}

std::unique_ptr<PlatformBuffer> PlatformBuffer::Create(uint64_t size)
{
    size = magma::round_up(size, PAGE_SIZE);
    if (size == 0)
        return DRETP(nullptr, "attempting to allocate 0 sized buffer");

    mx::vmo vmo;
    mx_status_t status = mx::vmo::create(size, 0, &vmo);
    if (status != NO_ERROR)
        return DRETP(nullptr, "failed to allocate vmo size %lld: %d", size, status);

    DLOG("allocated vmo size %ld handle 0x%x", size, vmo.get());
    return std::unique_ptr<PlatformBuffer>(new MagentaPlatformBuffer(std::move(vmo), size));
}

std::unique_ptr<PlatformBuffer> PlatformBuffer::Import(uint32_t handle)
{
    uint64_t size;
    // presumably this will fail if handle is invalid or not a vmo handle, so we perform no
    // additional error checking
    mx::vmo vmo(handle);
    auto status = vmo.get_size(&size);

    if (status != NO_ERROR)
        return DRETP(nullptr, "mx_vmo_get_size failed");

    if (!magma::is_page_aligned(size))
        return DRETP(nullptr, "attempting to import vmo with invalid size");

    return std::unique_ptr<PlatformBuffer>(new MagentaPlatformBuffer(std::move(vmo), size));
}
}
