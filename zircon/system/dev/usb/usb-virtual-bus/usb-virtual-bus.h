// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/usb/bus.h>
#include <ddktl/protocol/usb/dci.h>
#include <ddktl/protocol/usb/hci.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <lib/sync/completion.h>
#include <usb/request-cpp.h>

#include <threads.h>

#include "usb-virtual-device.h"
#include "usb-virtual-host.h"

namespace usb_virtual_bus {

class UsbVirtualBus;
class UsbVirtualDevice;
class UsbVirtualHost;
using UsbVirtualBusType = ddk::Device<UsbVirtualBus, ddk::Unbindable, ddk::Messageable>;

// This is the main class for the USB virtual bus.
class UsbVirtualBus : public UsbVirtualBusType {
public:
    explicit UsbVirtualBus(zx_device_t* parent)
        : UsbVirtualBusType(parent) {}

    static zx_status_t Create(zx_device_t* parent);

    // Device protocol implementation.
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
    void DdkUnbind();
    void DdkRelease();

    // USB device controller protocol implementation.
    void UsbDciRequestQueue(usb_request_t* usb_request, const usb_request_complete_t* complete_cb);
    zx_status_t UsbDciSetInterface(const usb_dci_interface_protocol_t* interface);
    zx_status_t UsbDciConfigEp(const usb_endpoint_descriptor_t* ep_desc,
                               const usb_ss_ep_comp_descriptor_t* ss_comp_desc);
    zx_status_t UsbDciDisableEp(uint8_t ep_address);
    zx_status_t UsbDciEpSetStall(uint8_t ep_address);
    zx_status_t UsbDciEpClearStall(uint8_t ep_address);
    zx_status_t UsbDciCancelAll(uint8_t endpoint);
    size_t UsbDciGetRequestSize();

    // USB host controller protocol implementation.
    void UsbHciRequestQueue(usb_request_t* usb_request, const usb_request_complete_t* complete_cb);
    void UsbHciSetBusInterface(const usb_bus_interface_protocol_t* bus_intf);
    size_t UsbHciGetMaxDeviceCount();
    zx_status_t UsbHciEnableEndpoint(uint32_t device_id, const usb_endpoint_descriptor_t* ep_desc,
                                     const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable);
    uint64_t UsbHciGetCurrentFrame();
    zx_status_t UsbHciConfigureHub(uint32_t device_id, usb_speed_t speed,
                                   const usb_hub_descriptor_t* desc, bool multi_tt);
    zx_status_t UsbHciHubDeviceAdded(uint32_t device_id, uint32_t port, usb_speed_t speed);
    zx_status_t UsbHciHubDeviceRemoved(uint32_t device_id, uint32_t port);
    zx_status_t UsbHciHubDeviceReset(uint32_t device_id, uint32_t port);
    zx_status_t UsbHciResetEndpoint(uint32_t device_id, uint8_t ep_address);
    zx_status_t UsbHciResetDevice(uint32_t hub_address, uint32_t device_id);
    size_t UsbHciGetMaxTransferSize(uint32_t device_id, uint8_t ep_address);
    zx_status_t UsbHciCancelAll(uint32_t device_id, uint8_t ep_address);
    size_t UsbHciGetRequestSize();

    // FIDL messages
    zx_status_t MsgEnable(fidl_txn_t* txn);
    zx_status_t MsgDisable(fidl_txn_t* txn);
    zx_status_t MsgConnect(fidl_txn_t* txn);
    zx_status_t MsgDisconnect(fidl_txn_t* txn);

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(UsbVirtualBus);

    using Request = usb::UnownedRequest<void>;
    using RequestQueue = usb::UnownedRequestQueue<void>;

    // This struct represents an endpoint on the virtual device.
    struct usb_virtual_ep_t {
        RequestQueue host_reqs;
        RequestQueue device_reqs;
        uint16_t max_packet_size = 0;
        // Offset into current host req, for dealing with host reqs that are bigger than
        // their matching device req.
        zx_off_t req_offset = 0;
        bool stalled = false;
    };

    zx_status_t Init();
    zx_status_t CreateDevice();
    zx_status_t CreateHost();
    void SetConnected(bool connected);
    int DeviceThread();
    void HandleControl(Request req);
    zx_status_t SetStall(uint8_t ep_address, bool stall);

    // Reference to class that implements the virtual device controller protocol.
    fbl::unique_ptr<UsbVirtualDevice> device_;
    // Reference to class that implements the virtual host controller protocol.
    fbl::unique_ptr<UsbVirtualHost> host_;

    // Callbacks to the USB peripheral driver.
    ddk::UsbDciInterfaceProtocolClient dci_intf_;
    // Callbacks to the USB bus driver.
    ddk::UsbBusInterfaceProtocolClient bus_intf_;

    usb_virtual_ep_t eps_[USB_MAX_EPS];

    thrd_t device_thread_;
    // Host-side lock
    fbl::Mutex lock_;
    fbl::ConditionVariable thread_signal_ __TA_GUARDED(lock_);

    // Device-side lock
    fbl::Mutex device_lock_ __TA_ACQUIRED_AFTER(lock_);
    fbl::ConditionVariable device_signal_ __TA_GUARDED(device_lock_);
    fbl::Mutex connection_lock_ __TA_ACQUIRED_BEFORE(device_lock_);
    // True when the virtual bus is connected.
    bool connected_ __TA_GUARDED(connection_lock_) = false;
    // Used to shut down our thread when this driver is unbinding.
    bool unbinding_ __TA_GUARDED(device_lock_) = false;
};

} // namespace usb_virtual_bus
