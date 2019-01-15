// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/usb/bus.h>
#include <ddktl/protocol/usb/hci.h>
#include <fbl/array.h>
#include <fbl/ref_ptr.h>

namespace usb_bus {

class UsbBus;
class UsbDevice;
using UsbBusType = ddk::Device<UsbBus, ddk::Unbindable>;

class UsbBus : public UsbBusType,
               public ddk::UsbBusProtocol<UsbBus, ddk::base_protocol>,
               public ddk::UsbBusInterface<UsbBus> {
public:
    UsbBus(zx_device_t* parent)
        : UsbBusType(parent), hci_(parent) {}

    static zx_status_t Create(void* ctx, zx_device_t* parent);

    // Device protocol implementation.
    void DdkUnbind();
    void DdkRelease();

    // USB Bus protocol implementation.
    zx_status_t UsbBusConfigureHub(zx_device_t* hub_device, usb_speed_t speed,
                                   const usb_hub_descriptor_t* desc);
    zx_status_t UsbBusDeviceAdded(zx_device_t* hub_device, uint32_t port, usb_speed_t speed);
    zx_status_t UsbBusDeviceRemoved(zx_device_t* hub_device, uint32_t port);
    zx_status_t UsbBusSetHubInterface(zx_device_t* usb_device, const usb_hub_interface_t* hub);

    // USB Bus interface implementation.
    zx_status_t UsbBusInterfaceAddDevice(uint32_t device_id, uint32_t hub_id, usb_speed_t speed);
    zx_status_t UsbBusInterfaceRemoveDevice(uint32_t device_id);
    zx_status_t UsbBusInterfaceResetPort(uint32_t hub_id, uint32_t port, bool enumerating);
    zx_status_t UsbBusInterfaceReinitializeDevice(uint32_t device_id);

private:
    zx_status_t Init();

    zx_status_t GetDeviceId(zx_device_t* device, uint32_t* out);

    // Our parent's HCI protocol.
    const ddk::UsbHciProtocolClient hci_;
    // Array of all our USB devices.
    fbl::Array<fbl::RefPtr<UsbDevice>> devices_;
};


} // namespace usb_bus
