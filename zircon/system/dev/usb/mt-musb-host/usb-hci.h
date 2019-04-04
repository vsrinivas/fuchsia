// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "usb-device.h"
#include "usb-root-hub.h"

#include <array>
#include <ddktl/device.h>
#include <ddktl/pdev.h>
#include <ddktl/protocol/usb/hci.h>
#include <ddktl/protocol/usb/bus.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/interrupt.h>
#include <memory>
#include <optional>
#include <threads.h>

namespace mt_usb_hci {

// The USB device id of the root hub.
constexpr uint32_t kRootHubId = 128;

// This corresponds to the 127 hardware-supported devices, the logical root-hub, and a reserved
// device-0 address used for enumeration.  Device addresses 0 and 128 are reserved for enumeration
// and the logical root-hub.
constexpr int kMaxDevices = 129;

class UsbHci;
using DeviceType = ddk::Device<UsbHci, ddk::Unbindable>;

// UsbHci provides the USB-HCI implementation.
class UsbHci : public DeviceType, public ddk::UsbHciProtocol<UsbHci, ddk::base_protocol> {
public:
    explicit UsbHci(zx_device_t* parent)
        : DeviceType(parent),
          pdev_(parent) {}

    ~UsbHci() = default;

    // Assign, copy, and move are disallowed.
    UsbHci(UsbHci&&) = delete;
    UsbHci& operator=(UsbHci&&) = delete;

    static zx_status_t Create(zx_device_t* parent);

    // Device protocol implementation.
    void DdkUnbind();
    void DdkRelease();

    // USB HCI protocol implementation.
    void UsbHciRequestQueue(usb_request_t* usb_request, const usb_request_complete_t* complete_cb);
    void UsbHciSetBusInterface(const usb_bus_interface_protocol_t* bus_intf);
    size_t UsbHciGetMaxDeviceCount();
    zx_status_t UsbHciEnableEndpoint(uint32_t device_id, const usb_endpoint_descriptor_t* desc,
                                     const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable);
    uint64_t UsbHciGetCurrentFrame();
    zx_status_t UsbHciConfigureHub(uint32_t device_id, usb_speed_t speed,
                                   const usb_hub_descriptor_t* desc);
    zx_status_t UsbHciHubDeviceAdded(uint32_t device_id, uint32_t port, usb_speed_t speed);
    zx_status_t UsbHciHubDeviceRemoved(uint32_t device_id, uint32_t port);
    zx_status_t UsbHciHubDeviceReset(uint32_t device_id, uint32_t port);
    zx_status_t UsbHciResetEndpoint(uint32_t device_id, uint8_t ep_address);
    zx_status_t UsbHciResetDevice(uint32_t hub_address, uint32_t device_id);
    size_t UsbHciGetMaxTransferSize(uint32_t device_id, uint8_t ep_address);
    zx_status_t UsbHciCancelAll(uint32_t device_id, uint8_t ep_address);
    size_t UsbHciGetRequestSize();

private:
    ddk::MmioBuffer* usb_mmio() { return &usb_mmio_.value(); }
    ddk::MmioBuffer* phy_mmio() { return &phy_mmio_.value(); }
    UsbRootHub* root_hub() { return static_cast<UsbRootHub*>(device_[kRootHubId].get()); }

    // Initialize the USB HCI.
    zx_status_t Init();

    // Initialize the given USB HCI sub-components.
    zx_status_t InitPhy();
    zx_status_t InitRootHub();
    zx_status_t InitFifo();

    // Start a USB session.
    void StartSession();

    // USB interrupt service routines.
    void HandleIrq();
    void HandleConnect();
    void HandleDisconnect();
    void HandleEndpoint(uint8_t ep);
    int IrqThread();

    // The zircon DDK platform device.
    ddk::PDev pdev_;

    // The usb register mmio.
    std::optional<ddk::MmioBuffer> usb_mmio_;

    // The usb phy register mmio.
    std::optional<ddk::MmioBuffer> phy_mmio_;

    // The system USB-common interrupt.  See MUSBMHDRC section 13.2.
    zx::interrupt irq_;

    // An async. thread responding to USB-common interrupt events.
    thrd_t irq_thread_;

    // The USB-bus device, used to announce new physical devices to the upper USB stack.
    ddk::UsbBusInterfaceProtocolClient bus_;

    // This is an array of UsbDevice unique_ptrs indexed by device_id.  Note that device_[0] is
    // reserved and should not be used.  Additionally, device_[128] is reserved for the logical usb
    // root-hub device.
    std::array<std::unique_ptr<UsbDevice>, kMaxDevices> device_;
};

} // namespace mt_usb_hci
