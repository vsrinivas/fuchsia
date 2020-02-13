// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_MT_MUSB_HOST_USB_ROOT_HUB_H_
#define SRC_DEVICES_USB_DRIVERS_MT_MUSB_HOST_USB_ROOT_HUB_H_

#include <endian.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/hub.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <usb/request-cpp.h>

#include "usb-device.h"

namespace mt_usb_hci {

namespace {

struct pvt_configuration_descriptor_t {
  usb_configuration_descriptor_t config;
  usb_interface_descriptor_t interface;
  usb_endpoint_descriptor_t endpoint;
};

}  // namespace

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
class UsbRootHub : public UsbDevice {
 public:
  explicit UsbRootHub(uint32_t id, ddk::MmioView usb) : id_(id), hub_id_(0), port_(usb) {}

  ~UsbRootHub() override = default;

  uint32_t id() const override { return id_; }
  uint32_t hub_id() const override { return hub_id_; }
  const usb_speed_t& speed() const override { return speed_; }

  zx_status_t HandleRequest(usb::BorrowedRequest<> req) override;
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
  zx_status_t ClearFeature(usb::BorrowedRequest<> req);
  zx_status_t ClearHubFeature(usb::BorrowedRequest<> req);
  zx_status_t ClearPortFeature(usb::BorrowedRequest<> req);
  // zx_status_t GetConfiguration(usb::BorrowedRequest<> req);
  zx_status_t GetDescriptor(usb::BorrowedRequest<> req);
  zx_status_t GetDeviceDescriptor(usb::BorrowedRequest<> req);
  zx_status_t GetConfigDescriptor(usb::BorrowedRequest<> req);
  zx_status_t GetStringDescriptor(usb::BorrowedRequest<> req);
  zx_status_t GetHubDescriptor(usb::BorrowedRequest<> req);
  // zx_status_t GetInterface(usb::BorrowedRequest<> req);
  zx_status_t GetStatus(usb::BorrowedRequest<> req);
  zx_status_t GetHubStatus(usb::BorrowedRequest<> req);
  zx_status_t GetPortStatus(usb::BorrowedRequest<> req);
  // zx_status_t SetAddress(usb::BorrowedRequest<> req);
  zx_status_t SetConfiguration(usb::BorrowedRequest<> req);
  // zx_status_t SetDescriptor(usb::BorrowedRequest<> req);
  zx_status_t SetFeature(usb::BorrowedRequest<> req);
  zx_status_t SetPortFeature(usb::BorrowedRequest<> req);
  zx_status_t SetHubFeature(usb::BorrowedRequest<> req);
  // zx_status_t SetInterface(usb::BorrowedRequest<> req);
  // zx_status_t SynchFrame(usb::BorrowedRequest<> req);

  // Endpoint-1 (aka get-port-status) handler, thread, and request queue.
  int EndpointHandlerThread();
  thrd_t endpoint_thread_ = {};
  usb::BorrowedRequestQueue<> endpoint_queue_;

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
      .bLength = sizeof(usb_device_descriptor_t),
      .bDescriptorType = USB_DT_DEVICE,
      .bcdUSB = htole16(0x0200),
      .bDeviceClass = USB_CLASS_HUB,
      .bDeviceSubClass = 0,
      .bDeviceProtocol = 1,
      .bMaxPacketSize0 = 64,
      .idVendor = htole16(0x18d1),
      .idProduct = htole16(0xa001),
      .bcdDevice = htole16(0x0100),
      .iManufacturer = 1,
      .iProduct = 2,
      .iSerialNumber = 0,
      .bNumConfigurations = 1,
  };

  static constexpr pvt_configuration_descriptor_t config_descriptor_ = {
      .config =
          {
              .bLength = sizeof(usb_configuration_descriptor_t),
              .bDescriptorType = USB_DT_CONFIG,
              .wTotalLength = htole16(sizeof(pvt_configuration_descriptor_t)),
              .bNumInterfaces = 1,
              .bConfigurationValue = 1,
              .iConfiguration = 0,
              .bmAttributes = 0xe0,  // self-powered.
              .bMaxPower = 0,
          },
      .interface =
          {
              .bLength = sizeof(usb_interface_descriptor_t),
              .bDescriptorType = USB_DT_INTERFACE,
              .bInterfaceNumber = 0,
              .bAlternateSetting = 0,
              .bNumEndpoints = 1,
              .bInterfaceClass = USB_CLASS_HUB,
              .bInterfaceSubClass = 0,
              .bInterfaceProtocol = 0,
              .iInterface = 0,
          },
      .endpoint =
          {
              // USB hub status change endpoint
              .bLength = sizeof(usb_endpoint_descriptor_t),
              .bDescriptorType = USB_DT_ENDPOINT,
              .bEndpointAddress = USB_ENDPOINT_IN | 1,
              .bmAttributes = USB_ENDPOINT_INTERRUPT,
              .wMaxPacketSize = htole16(4),
              .bInterval = 12,
          },
  };

  static constexpr uint8_t string_lang_descriptor_[] = {
      4,              // .bLength
      USB_DT_STRING,  // .bDescriptorType
      0x09, 0x04,     // .bString (EN-US as the only supported language)
  };

  static constexpr uint8_t string_mfr_descriptor_[] = {
      14,             // .bLength
      USB_DT_STRING,  // .bDescriptorType
      'Z',
      0,
      'i',
      0,
      'r',
      0,  // .bString
      'c',
      0,
      'o',
      0,
      'n',
      0,  // "Zircon", UTF-16LE
  };

  static constexpr uint8_t string_product_descriptor_[] = {
      34,             // .bLength
      USB_DT_STRING,  // .bDescriptorType
      'U',
      0,
      'S',
      0,
      'B',
      0,
      ' ',
      0,  // .bString
      '2',
      0,
      '.',
      0,
      '0',
      0,
      ' ',
      0,
      'R',
      0,
      'o',
      0,
      'o',
      0,
      't',
      0,
      ' ',
      0,
      'H',
      0,
      'u',
      0,
      'b',
      0,  // "USB 2.0 Root Hub", UTF-16LE
  };

  const usb_string_descriptor_t* const string_descriptor_[3] = {
      reinterpret_cast<const usb_string_descriptor_t*>(&string_lang_descriptor_),
      reinterpret_cast<const usb_string_descriptor_t*>(&string_mfr_descriptor_),
      reinterpret_cast<const usb_string_descriptor_t*>(&string_product_descriptor_),
  };

  static constexpr usb_hub_descriptor_t hub_descriptor_ = {
      .bDescLength = sizeof(usb_hub_descriptor_t),
      .bDescriptorType = USB_HUB_DESC_TYPE,
      .bNbrPorts = 1,
      .wHubCharacteristics = 0,
      .bPowerOn2PwrGood = 1,
      .bHubContrCurrent = 0,
      .hs =
          {
              .DeviceRemovable = {0, 0, 0, 0},
              .PortPwrCtrlMask = {0, 0, 0, 0},
          },
  };
};

}  // namespace mt_usb_hci

#endif  // SRC_DEVICES_USB_DRIVERS_MT_MUSB_HOST_USB_ROOT_HUB_H_
