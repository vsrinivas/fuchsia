// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/platform/bus.h>
#include <ddktl/protocol/platform/device.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>

#include "device-resources.h"
#include "proxy-protocol.h"

namespace platform_bus {

class PlatformBus;

class CompositeDevice;
using CompositeDeviceType = ddk::Device<CompositeDevice>;

// This class is used for composite platform devices.
class CompositeDevice : public CompositeDeviceType,
                        public ddk::PDevProtocol<CompositeDevice, ddk::base_protocol> {
public:
    // Creates a new CompositeDevice instance.
    static zx_status_t Create(const pbus_dev_t* pdev, zx_device_t* parent, PlatformBus* bus,
                              fbl::unique_ptr<platform_bus::CompositeDevice>* out);

    inline uint32_t vid() const { return vid_; }
    inline uint32_t pid() const { return pid_; }
    inline uint32_t did() const { return did_; }

    // Device protocol implementation.
    void DdkRelease();

    // Platform device implementation protocol implementation.
    zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio);
    zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq);
    zx_status_t PDevGetBti(uint32_t index, zx::bti* out_bti);
    zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_smc);
    zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info);
    zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info);
    zx_status_t PDevDeviceAdd(uint32_t index, const device_add_args_t* args,
                              zx_device_t** out_device);
    zx_status_t PDevGetProtocol(uint32_t proto_id, uint32_t index, void* out_out_protocol_buffer,
                                size_t out_protocol_size, size_t* out_out_protocol_actual);

    // Starts the underlying devmgr device.
    zx_status_t Start();

private:
    explicit CompositeDevice(zx_device_t* parent, PlatformBus* bus, const pbus_dev_t* pdev);
    zx_status_t Init(const pbus_dev_t* pdev);

    PlatformBus* bus_;
    char name_[ZX_DEVICE_NAME_MAX + 1];
    const uint32_t vid_;
    const uint32_t pid_;
    const uint32_t did_;

    // Platform bus resources for this device.
    DeviceResources resources_;
};

} // namespace platform_bus
