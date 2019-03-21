// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "usb-device.h"

#include <endian.h>
#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <lib/mmio/mmio.h>
#include <memory>
#include <threads.h>
#include <usb/request-cpp.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/hub.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>
#include <zircon/thread_annotations.h>

namespace mt_usb_hci {

namespace {

struct pvt_configuration_descriptor_t {
    usb_configuration_descriptor_t config;
    usb_interface_descriptor_t interface;
    usb_endpoint_descriptor_t endpoint;
};

} // namespace

// HubPort represents a hub's physical port.
class HubPort {
public:
    explicit HubPort(ddk::MmioView usb) : usb_(usb) {}

    ~HubPort() = default;

    const usb_port_status_t& status() const {
        fbl::AutoLock _(&status_lock_);
        return status_;
    }

    bool connected() const { return connected_; }

    // A new device was connected to this port.  Notify waiting threads.
    void Connect();

    // A device was removed from this port.  Notify waiting threads.
    void Disconnect();

    // Disable the port.
    void Disable();

    // Enable reset-signaling on the USB PHY.  PORT_RESET will be cleared after the hardware
    // finishes the reset-signaling routine.
    void Reset();

    // Disable power to the port.
    void PowerOff();

    // Enable power to the port.
    void PowerOn();

    // Suspend the port.
    void Suspend();

    // Resume the port.
    void Resume();

    // Clear the masked port change bits.
    void ClearChangeBits(int mask);

    // Block and wait for a change to the port's physical connectivity.
    void Wait();

private:
    // The USB register mmio.
    ddk::MmioView usb_;

    // Lock guarding composite status_ RMW semantics.
    mutable fbl::Mutex status_lock_;

    // The current port status.
    usb_port_status_t status_ TA_GUARDED(status_lock_);

    // Condition (and associated lock) signaling a port status change occured.
    fbl::Mutex change_lock_;
    fbl::ConditionVariable change_ TA_GUARDED(change_lock_);

    // True if there is a device attached to this port.
    bool connected_;
};

// UsbRootHub is the logical USB 2.0 root hub device.  The chipset does not contain a root hub
// controller, so we emulate the device here.  Because this is the root hub, it is assumed this
// will be a singleton instance.
class UsbRootHub: public UsbDevice {
public:
    explicit UsbRootHub(uint32_t id, ddk::MmioView usb)
        : id_(id),
          hub_id_(0),
          port_(usb) {}

    ~UsbRootHub() override = default;

    uint32_t id() const override { return id_; }
    uint32_t hub_id() const override { return hub_id_; }
    const usb_speed_t& speed() const override { return speed_; }

    zx_status_t HandleRequest(usb::UnownedRequest<> req) override;
    zx_status_t EnableEndpoint(const usb_endpoint_descriptor_t&) override { return ZX_OK; }
    zx_status_t DisableEndpoint(const usb_endpoint_descriptor_t&) override { return ZX_OK; }
    size_t GetMaxTransferSize(uint8_t) override { return 0; }

    // A new device was attached to the port.
    zx_status_t PortConnect();

    // A device was removed from the port.
    zx_status_t PortDisconnect();

    // Enable reset signaling for the hub's port.
    zx_status_t PortReset();

private:
    // TODO(hansens) Implement the rest of the std. device requests should the stack ever use them.
    zx_status_t ClearFeature(usb::UnownedRequest<> req);
    zx_status_t ClearHubFeature(usb::UnownedRequest<> req);
    zx_status_t ClearPortFeature(usb::UnownedRequest<> req);
    //zx_status_t GetConfiguration(usb::UnownedRequest<> req);
    zx_status_t GetDescriptor(usb::UnownedRequest<> req);
    zx_status_t GetDeviceDescriptor(usb::UnownedRequest<> req);
    zx_status_t GetConfigDescriptor(usb::UnownedRequest<> req);
    zx_status_t GetStringDescriptor(usb::UnownedRequest<> req);
    zx_status_t GetHubDescriptor(usb::UnownedRequest<> req);
    //zx_status_t GetInterface(usb::UnownedRequest<> req);
    zx_status_t GetStatus(usb::UnownedRequest<> req);
    zx_status_t GetHubStatus(usb::UnownedRequest<> req);
    zx_status_t GetPortStatus(usb::UnownedRequest<> req);
    //zx_status_t SetAddress(usb::UnownedRequest<> req);
    zx_status_t SetConfiguration(usb::UnownedRequest<> req);
    //zx_status_t SetDescriptor(usb::UnownedRequest<> req);
    zx_status_t SetFeature(usb::UnownedRequest<> req);
    zx_status_t SetPortFeature(usb::UnownedRequest<> req);
    zx_status_t SetHubFeature(usb::UnownedRequest<> req);
    //zx_status_t SetInterface(usb::UnownedRequest<> req);
    //zx_status_t SynchFrame(usb::UnownedRequest<> req);

    // Endpoint-1 (aka get-port-status) handler, thread, and request queue.
    int EndpointHandlerThread();
    thrd_t endpoint_thread_ = {};
    usb::UnownedRequestQueue<> endpoint_queue_;

    // The USB device id (address) for this root hub.
    uint32_t id_;

    // This device's parent hub.  Because this is the root hub, it is not attached to a hub and this
    // value is initialized to 0.
    uint32_t hub_id_;

    // The single physical port provided by this hub.
    HubPort port_;

    // The hub's maximum speed.
    static constexpr usb_speed_t speed_ = USB_SPEED_HIGH;

    // USB root hub device descriptors.
    static constexpr usb_device_descriptor_t device_descriptor_ = {
        sizeof(usb_device_descriptor_t), // .bLength
        USB_DT_DEVICE,   // .bDescriptorType
        htole16(0x0200), // .bcdUSB
        USB_CLASS_HUB,   // .bDeviceClass
        0,               // .bDeviceSubClass
        1,               // .bDeviceProtocol
        64,              // .bMaxPacketSize0
        htole16(0x18d1), // .idVendor
        htole16(0xa001), // .idProduct
        htole16(0x0100), // .bcdDevice
        1,               // .iManufacturer
        2,               // .iProduct
        0,               // .iSerialNumber
        1,               // .bNumConfigurations
    };

    static constexpr pvt_configuration_descriptor_t config_descriptor_ = {
        .config = {
            sizeof(usb_configuration_descriptor_t),          // .bLength
            USB_DT_CONFIG,                                   // .bDescriptorType
            htole16(sizeof(pvt_configuration_descriptor_t)), // .wTotalLength
            1,                      // .bNumInterfaces
            1,                      // .bConfigurationValue
            0,                      // .iConfiguration
            0xe0,                   // .bmAttributes (self powered)
            0,                      // .bMaxPower
        },
        .interface = {
            sizeof(usb_interface_descriptor_t), // .bLength
            USB_DT_INTERFACE,       // .bDescriptorType
            0,                      // .bInterfaceNumber
            0,                      // .bAlternateSetting
            1,                      // .bNumEndpoints
            USB_CLASS_HUB,          // .bInterfaceClass
            0,                      // .bInterfaceSubClass
            0,                      // .bInterfaceProtocol
            0,                      // .iInterface
        },
        .endpoint = { // USB hub status change endpoint
            sizeof(usb_endpoint_descriptor_t), // .bLength
            USB_DT_ENDPOINT,        // .bDescriptorType
            USB_ENDPOINT_IN | 1,    // .bEndpointAddress
            USB_ENDPOINT_INTERRUPT, // .bmAttributes
            htole16(4),             // .wMaxPacketSize
            12,                     // .bInterval
        },
    };

    static constexpr uint8_t string_lang_descriptor_[] = {
        4,                          // .bLength
        USB_DT_STRING,              // .bDescriptorType
        0x09, 0x04,                 // .bString (EN-US as the only supported language)
    };

    static constexpr uint8_t string_mfr_descriptor_[] = {
        14,                         // .bLength
        USB_DT_STRING,              // .bDescriptorType
        'Z', 0, 'i', 0, 'r', 0,     // .bString
        'c', 0, 'o', 0, 'n', 0,     // "Zircon", UTF-16LE
    };

    static constexpr uint8_t string_product_descriptor_[] = {
        34,                             // .bLength
        USB_DT_STRING,                  // .bDescriptorType
        'U', 0, 'S', 0, 'B', 0, ' ', 0, // .bString
        '2', 0, '.', 0, '0', 0, ' ', 0,
        'R', 0, 'o', 0, 'o', 0, 't', 0,
        ' ', 0, 'H', 0, 'u', 0, 'b', 0, // "USB 2.0 Root Hub", UTF-16LE
    };

    const usb_string_descriptor_t* const string_descriptor_[3] = {
        reinterpret_cast<const usb_string_descriptor_t*>(&string_lang_descriptor_),
        reinterpret_cast<const usb_string_descriptor_t*>(&string_mfr_descriptor_),
        reinterpret_cast<const usb_string_descriptor_t*>(&string_product_descriptor_),
    };

    static constexpr usb_hub_descriptor_t hub_descriptor_ = {
        sizeof(usb_hub_descriptor_t),   // .bDescLength
        USB_HUB_DESC_TYPE,              // .bDescriptorType
        1,                              // .bNbrPorts
        0,                              // .wHubCharacteristics
        1,                              // .bPwrOn2PwrGood
        0,                              // .bHubContrCurrent
        {{{0, 0, 0, 0}, {0, 0, 0, 0}}}, // .struct-hs (unused)
    };
};

} // namespace mt_usb_hci
