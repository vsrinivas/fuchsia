// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/platform-bus.h>
#include <ddktl/protocol/platform-device.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>

#include "device-resources.h"
#include "proxy-protocol.h"

namespace platform_bus {

class PlatformBus;

// This class is used for binding protocol implementation drivers.
// It implements the platform device protocol, and also provides access to the
// a subset of the platform bus protocol, and also the other protocols that are
// available to platform devices.
// Unlike platform device drivers, proto implementation drivers run in the same
// devhost as the platform bus driver.

class ProtocolDevice;
using ProtocolDeviceType = ddk::Device<ProtocolDevice, ddk::GetProtocolable>;

// This class represents a platform device attached to the platform bus.
// Instances of this class are created by PlatformBus at boot time when the board driver
// calls the platform bus protocol method pbus_device_add().

class ProtocolDevice : public ProtocolDeviceType, public ddk::PDevProtocol<ProtocolDevice> {
public:
    // Creates a new ProtocolDevice instance.
    // *flags* contains zero or more PDEV_ADD_* flags from the platform bus protocol.
    static zx_status_t Create(const pbus_dev_t* pdev, zx_device_t* parent, PlatformBus* bus,
                              fbl::unique_ptr<platform_bus::ProtocolDevice>* out);

    inline uint32_t vid() const { return vid_; }
    inline uint32_t pid() const { return pid_; }
    inline uint32_t did() const { return did_; }

    // Device protocol implementation.
    zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
    void DdkRelease();

    // Platform device protocol implementation.
    zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio);
    zx_status_t PDevMapMmio(uint32_t index, uint32_t cache_policy, void** out_vaddr,
                            size_t* out_size, zx_paddr_t* out_paddr, zx_handle_t* out_handle);
    zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle);
    zx_status_t PDevGetBti(uint32_t index, zx_handle_t* out_handle);
    zx_status_t PDevGetSmc(uint32_t index, zx_handle_t* out_handle);
    zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info);
    zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info);
    zx_status_t PDevDeviceAdd(uint32_t index, const device_add_args_t* args, zx_device_t** device);
    zx_status_t PDevGetProtocol(uint32_t proto_id, uint32_t index, void* out_protocol,
                                size_t protocol_size, size_t* protocol_actual);

    // Starts the underlying devmgr device.
    zx_status_t Start();

private:
    explicit ProtocolDevice(zx_device_t* parent, PlatformBus* bus, const pbus_dev_t* pdev);
    zx_status_t Init(const pbus_dev_t* pdev);

    PlatformBus* bus_;
    char name_[ZX_DEVICE_NAME_MAX + 1];
    const uint32_t vid_;
    const uint32_t pid_;
    const uint32_t did_;

    // Platform bus resources for this device.
    DeviceResources resources_;

    // Restricted subset of the platform bus protocol.
    // We do not allow protocol devices call pbus_device_add() or pbus_protocol_device_add()
    pbus_protocol_ops_t pbus_ops_;
    void* pbus_ctx_;
};

} // namespace platform_bus
