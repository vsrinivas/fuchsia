// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_MISC_GOLDFISH_ADDRESS_SPACE_ADDRESS_SPACE_DEVICE_H_
#define ZIRCON_SYSTEM_DEV_MISC_GOLDFISH_ADDRESS_SPACE_ADDRESS_SPACE_DEVICE_H_

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/pci.h>
#include <fbl/mutex.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/bti.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

namespace goldfish {

class AddressSpaceDevice;
using DeviceType =
    ddk::Device<AddressSpaceDevice, ddk::Unbindable, ddk::Openable>;

class AddressSpaceDevice : public DeviceType {
public:
    static zx_status_t Create(void* ctx, zx_device_t* parent);

    explicit AddressSpaceDevice(zx_device_t* parent);
    ~AddressSpaceDevice();

    zx_status_t Bind();

    uint32_t AllocateBlock(uint64_t* size, uint64_t* offset);
    uint32_t DeallocateBlock(uint64_t offset);
    zx_status_t PinBlock(uint64_t offset, uint64_t size, zx_paddr_t* paddr,
                         zx::pmt* pmt);

    // Device protocol implementation.
    zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
    void DdkUnbind();
    void DdkRelease();

private:
    uint32_t CommandMmioLocked(uint32_t cmd) TA_REQ(mmio_lock_);

    ddk::PciProtocolClient pci_;
    zx::bti bti_;
    zx::vmo dma_region_;

    fbl::Mutex mmio_lock_;
    std::optional<ddk::MmioBuffer> mmio_ TA_GUARDED(mmio_lock_);

    DISALLOW_COPY_ASSIGN_AND_MOVE(AddressSpaceDevice);
};

} // namespace goldfish

#endif // ZIRCON_SYSTEM_DEV_MISC_GOLDFISH_ADDRESS_SPACE_ADDRESS_SPACE_DEVICE_H_
