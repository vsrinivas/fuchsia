// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_pci_device.h"
#include <ddk/protocol/intel-gpu-core.h>

class GttIntelGpuCore : public Gtt {
public:
    class Owner {
    public:
        virtual zx_intel_gpu_core_protocol_ops_t* ops() = 0;
        virtual void* context() = 0;
    };

    GttIntelGpuCore(Owner* owner) : owner_(owner) {}

    uint64_t Size() const override { return owner_->ops()->gtt_get_size(owner_->context()); }

    // Init only for core gtt
    bool Init(uint64_t gtt_size) override
    {
        DASSERT(false);
        return false;
    }

    // AddressSpace overrides
    bool Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out) override
    {
        DASSERT(size % PAGE_SIZE == 0);
        // Always page aligned.
        zx_status_t status =
            owner_->ops()->gtt_alloc(owner_->context(), size / PAGE_SIZE, addr_out);
        if (status != ZX_OK)
            return DRETF(false, "gtt_alloc failed: %d", status);
        return true;
    }

    bool Free(uint64_t addr) override
    {
        zx_status_t status = owner_->ops()->gtt_free(owner_->context(), addr);
        if (status != ZX_OK)
            return DRETF(false, "gtt_free failed: %d", status);
        return status;
    }

    bool Clear(uint64_t addr) override
    {
        zx_status_t status = owner_->ops()->gtt_clear(owner_->context(), addr);
        if (status != ZX_OK)
            return DRETF(false, "gtt_clear failed: %d", status);
        return true;
    }

    bool Insert(uint64_t addr, uint32_t buffer_handle, uint64_t offset, uint64_t length,
                CachingType caching_type) override
    {
        DASSERT(offset % PAGE_SIZE == 0);
        DASSERT(length % PAGE_SIZE == 0);
        zx_status_t status = owner_->ops()->gtt_insert(owner_->context(), addr, buffer_handle,
                                                       offset / PAGE_SIZE, length / PAGE_SIZE);
        if (status != ZX_OK)
            return DRETF(false, "gtt_insert failed: %d", status);
        return true;
    }

private:
    Owner* owner_;
};

class MsdIntelPciMmio : public magma::PlatformMmio {
public:
    class Owner {
    public:
        virtual zx_intel_gpu_core_protocol_ops_t* ops() = 0;
        virtual void* context() = 0;
    };

    MsdIntelPciMmio(Owner* owner, void* addr, uint64_t size, uint32_t pci_bar)
        : magma::PlatformMmio(addr, size), owner_(owner), pci_bar_(pci_bar)
    {
    }

    ~MsdIntelPciMmio() { owner_->ops()->unmap_pci_mmio(owner_->context(), pci_bar_); }

private:
    Owner* owner_;
    uint32_t pci_bar_;
};

class MsdIntelPciDeviceShim : public MsdIntelPciDevice,
                              public GttIntelGpuCore::Owner,
                              public MsdIntelPciMmio::Owner {
public:
    MsdIntelPciDeviceShim(zx_intel_gpu_core_protocol_t* intel_gpu_core)
        : intel_gpu_core_(intel_gpu_core), gtt_(this)
    {
    }

    void* GetDeviceHandle() override { return intel_gpu_core_; }

    bool ReadPciConfig16(uint64_t addr, uint16_t* value) override
    {
        zx_status_t status = ops()->read_pci_config_16(context(), addr, value);
        if (status != ZX_OK)
            return DRETF(false, "read_pci_config_16 failed: %d", status);
        return true;
    }

    std::unique_ptr<magma::PlatformMmio>
    CpuMapPciMmio(unsigned int pci_bar, magma::PlatformMmio::CachePolicy cache_policy) override
    {
        void* addr;
        uint64_t size;
        zx_status_t status = ops()->map_pci_mmio(context(), pci_bar, &addr, &size);
        if (status != ZX_OK)
            return DRETP(nullptr, "map_pci_mmio failed: %d", status);
        return std::make_unique<MsdIntelPciMmio>(this, addr, size, pci_bar);
    }

    bool RegisterInterruptCallback(InterruptManager::InterruptCallback callback, void* data,
                                   uint32_t interrupt_mask) override
    {
        zx_status_t status =
            ops()->register_interrupt_callback(context(), callback, data, interrupt_mask);
        if (status != ZX_OK)
            return DRETF(false, "register_interrupt_callback failed: %d", status);
        return true;
    }

    void UnregisterInterruptCallback() override { ops()->unregister_interrupt_callback(context()); }

    Gtt* GetGtt() override { return &gtt_; }

    void PresentBuffer(uint32_t buffer_handle, magma_system_image_descriptor* image_desc,
                       std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores,
                       std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores,
                       present_buffer_callback_t callback) override
    {
        DASSERT(false);
    }

    zx_intel_gpu_core_protocol_ops_t* ops() override { return intel_gpu_core_->ops; }

    void* context() override { return intel_gpu_core_->ctx; }

private:
    zx_intel_gpu_core_protocol_t* intel_gpu_core_;
    GttIntelGpuCore gtt_;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<MsdIntelPciDevice> MsdIntelPciDevice::CreateShim(void* platform_device_handle)
{
    return std::make_unique<MsdIntelPciDeviceShim>(
        reinterpret_cast<zx_intel_gpu_core_protocol_t*>(platform_device_handle));
}
