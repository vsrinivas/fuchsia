// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-root-hub.h"

#include <lib/zx/time.h>
#include <zircon/status.h>

#include <algorithm>
#include <memory>
#include <optional>

#include <ddk/debug.h>
#include <soc/mt8167/mt8167-usb.h>

namespace mt_usb_hci {
namespace regs = board_mt8167;

void HubPort::Connect() {
  // We need to atomically both update the port status bits and signal a port status change.
  fbl::AutoLock _(&status_lock_);

  status_.wPortChange |= USB_C_PORT_CONNECTION;
  status_.wPortStatus |= USB_PORT_CONNECTION | USB_PORT_ENABLE | USB_PORT_POWER;
  connected_ = true;

  {
    fbl::AutoLock _(&change_lock_);
    change_.Signal();
  }
}

void HubPort::Disconnect() {
  fbl::AutoLock _(&status_lock_);

  status_.wPortChange |= USB_C_PORT_CONNECTION;
  status_.wPortStatus &= static_cast<uint16_t>(~(USB_PORT_CONNECTION | USB_PORT_ENABLE));

  connected_ = false;

  {
    fbl::AutoLock _(&change_lock_);
    change_.Broadcast();
  }
}

void HubPort::Disable() {
  fbl::AutoLock _(&status_lock_);
  status_.wPortStatus &= static_cast<uint16_t>(~USB_PORT_ENABLE);
}

void HubPort::Reset() {
  fbl::AutoLock _(&status_lock_);
  status_.wPortStatus |= USB_PORT_RESET;

  auto power = regs::POWER_HOST::Get().ReadFrom(&usb_);
  power.set_hsenab(1).set_reset(1).WriteTo(&usb_);
  // Controller spec. requires at least 20ms for speed negotiation.
  zx::nanosleep(zx::deadline_after(zx::msec(25)));
  power.set_reset(0).WriteTo(&usb_);

  // Determine the controller's post-reset negotiated speed.
  power = regs::POWER_HOST::Get().ReadFrom(&usb_);
  auto devctl = regs::DEVCTL::Get().ReadFrom(&usb_);
  if (devctl.lsdev()) {  // Low-speed mode.
    status_.wPortStatus &= static_cast<uint16_t>(~USB_PORT_HIGH_SPEED);
    status_.wPortStatus |= USB_PORT_LOW_SPEED;
  } else if (power.hsmode()) {  // High-speed mode.
    status_.wPortStatus &= static_cast<uint16_t>(~USB_PORT_LOW_SPEED);
    status_.wPortStatus |= USB_PORT_HIGH_SPEED;
  } else {  // Full-speed mode.
    status_.wPortStatus &= static_cast<uint16_t>(~USB_PORT_LOW_SPEED);
    status_.wPortStatus &= static_cast<uint16_t>(~USB_PORT_HIGH_SPEED);
  }

  // See: 11.24.2.13 (USB 2.0 spec)
  status_.wPortStatus |= USB_PORT_ENABLE;
  status_.wPortStatus &= static_cast<uint16_t>(~USB_PORT_RESET);
  status_.wPortChange |= USB_C_PORT_RESET;
}

void HubPort::PowerOff() {
  fbl::AutoLock _(&status_lock_);
  status_.wPortStatus &= static_cast<uint16_t>(~USB_PORT_POWER);
}

void HubPort::PowerOn() {
  fbl::AutoLock _(&status_lock_);
  status_.wPortStatus |= USB_PORT_POWER;
}

void HubPort::Suspend() {
  fbl::AutoLock _(&status_lock_);
  status_.wPortStatus |= USB_PORT_SUSPEND;
}

void HubPort::Resume() {
  fbl::AutoLock _(&status_lock_);
  status_.wPortStatus &= static_cast<uint16_t>(~USB_PORT_SUSPEND);
}

void HubPort::ClearChangeBits(int mask) {
  fbl::AutoLock _(&status_lock_);
  status_.wPortChange &= static_cast<uint16_t>(~mask);
}

void HubPort::Wait() {
  if (!(status().wPortChange & USB_C_PORT_CONNECTION)) {
    // Wait only if there is no outstanding port status change.
    fbl::AutoLock _(&change_lock_);
    change_.Wait(&change_lock_);
  }
}

zx_status_t UsbRootHub::HandleRequest(usb::BorrowedRequest<> req) {
  uint8_t ep_addr = static_cast<uint8_t>(req.request()->header.ep_address & 0xf);

  if (ep_addr > 1) {  // A USB hub only suports two endpoints: control and interrupt.
    zxlogf(ERROR, "unsupported hub endpoint address: %d\n", ep_addr);
    req.Complete(ZX_ERR_INTERNAL, 0);
    return ZX_ERR_INTERNAL;
  }

  if (ep_addr == 0) {  // Endpoint-0 control transfers.
    switch (req.request()->setup.bRequest) {
      case USB_REQ_GET_DESCRIPTOR:
        return GetDescriptor(std::move(req));
      case USB_REQ_SET_CONFIGURATION:
        return SetConfiguration(std::move(req));
      case USB_REQ_SET_FEATURE:
        return SetFeature(std::move(req));
      case USB_REQ_GET_STATUS:
        return GetStatus(std::move(req));
      case USB_REQ_CLEAR_FEATURE:
        return ClearFeature(std::move(req));
      default:
        zxlogf(ERROR, "unsupported device request: 0x%02x\n", req.request()->setup.bRequest);
        req.Complete(ZX_ERR_NOT_SUPPORTED, 0);
        return ZX_ERR_NOT_SUPPORTED;
    }
  } else {  // Endpoint-1 port-status interrupt transfers.
    endpoint_queue_.push(std::move(req));

    // Defer endpoint completion until we know we have activity on the port.
    auto t = [](void* arg) { return static_cast<UsbRootHub*>(arg)->EndpointHandlerThread(); };
    auto status = thrd_create_with_name(&endpoint_thread_, t, this, "hub_endpoint_thread");
    if (status != ZX_OK) {
      zxlogf(ERROR, "root hub thread init error: %s\n", zx_status_get_string(status));
      req.Complete(status, 0);
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t UsbRootHub::PortConnect() {
  port_.Connect();
  return ZX_OK;
}

zx_status_t UsbRootHub::PortDisconnect() {
  port_.Disconnect();
  return ZX_OK;
}

zx_status_t UsbRootHub::PortReset() {
  port_.Reset();
  return ZX_OK;
}

zx_status_t UsbRootHub::ClearFeature(usb::BorrowedRequest<> req) {
  uint16_t index = le16toh(req.request()->setup.wIndex);
  if (index != 1) {
    zxlogf(ERROR, "unsupported ClearFeature() index: %d\n", index);
    req.Complete(ZX_ERR_OUT_OF_RANGE, 0);
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint8_t bm_req_type = req.request()->setup.bmRequestType;
  switch (bm_req_type) {
    case 0x20:  // See: 11.24.2 (USB 2.0 spec)
      return ClearHubFeature(std::move(req));
    case 0x23:  // See: 11.24.2 (USB 2.0 spec)
      return ClearPortFeature(std::move(req));
    default:
      zxlogf(ERROR, "unsupported ClearFeature() request type: 0x%02x\n", bm_req_type);
      req.Complete(ZX_ERR_NOT_SUPPORTED, 0);
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t UsbRootHub::ClearHubFeature(usb::BorrowedRequest<> req) {
  // Currently hub-level features are not supported.
  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  req.Complete(status, 0);
  return status;
}

zx_status_t UsbRootHub::ClearPortFeature(usb::BorrowedRequest<> req) {
  uint16_t feature = static_cast<uint16_t>(letoh16(req.request()->setup.wValue));

  switch (feature) {
    case USB_FEATURE_PORT_ENABLE:
      port_.Disable();
      break;
    case USB_FEATURE_PORT_SUSPEND:
      port_.Resume();
      break;
    case USB_FEATURE_PORT_POWER:
      port_.PowerOff();
      break;
    case USB_FEATURE_C_PORT_CONNECTION:
      port_.ClearChangeBits(USB_C_PORT_CONNECTION);
      break;
    case USB_FEATURE_C_PORT_RESET:
      port_.ClearChangeBits(USB_C_PORT_RESET);
      break;
    case USB_FEATURE_C_PORT_ENABLE:
      port_.ClearChangeBits(USB_C_PORT_ENABLE);
      break;
    case USB_FEATURE_C_PORT_SUSPEND:
      port_.ClearChangeBits(USB_C_PORT_SUSPEND);
      break;
    case USB_FEATURE_C_PORT_OVER_CURRENT:
      port_.ClearChangeBits(USB_C_PORT_OVER_CURRENT);
      break;
    default:
      zxlogf(ERROR, "unsupported ClearFeature() selector: 0x%02x\n", feature);
      req.Complete(ZX_ERR_INVALID_ARGS, 0);
      return ZX_ERR_INVALID_ARGS;
  }

  req.Complete(ZX_OK, 0);
  return ZX_OK;
}

zx_status_t UsbRootHub::GetDescriptor(usb::BorrowedRequest<> req) {
  uint8_t type = static_cast<uint8_t>(req.request()->setup.wValue >> 8);

  switch (type) {
    case USB_DT_DEVICE:
      return GetDeviceDescriptor(std::move(req));
    case USB_DT_CONFIG:
      return GetConfigDescriptor(std::move(req));
    case USB_DT_STRING:
      return GetStringDescriptor(std::move(req));
    case USB_HUB_DESC_TYPE:  // HUB-class descriptor.
      return GetHubDescriptor(std::move(req));
    default:
      zxlogf(ERROR, "unsupported GetDescriptor() descriptor type: 0x%02x\n", type);
      req.Complete(ZX_ERR_NOT_SUPPORTED, 0);
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t UsbRootHub::GetDeviceDescriptor(usb::BorrowedRequest<> req) {
  uint16_t len = le16toh(req.request()->setup.wLength);
  ZX_ASSERT(len <= sizeof(usb_device_descriptor_t));
  ssize_t actual = req.CopyTo(&device_descriptor_, len, 0);
  req.Complete(ZX_OK, actual);
  return ZX_OK;
}

zx_status_t UsbRootHub::GetConfigDescriptor(usb::BorrowedRequest<> req) {
  uint8_t index = static_cast<uint8_t>(req.request()->setup.wValue & 0xff);
  size_t len = static_cast<size_t>(le16toh(req.request()->setup.wLength));

  if (index > 0) {
    req.Complete(ZX_ERR_OUT_OF_RANGE, 0);
    return ZX_ERR_OUT_OF_RANGE;
  }

  size_t w_total_len = static_cast<size_t>(le16toh(config_descriptor_.config.wTotalLength));
  len = std::min(len, w_total_len);

  ssize_t actual = req.CopyTo(&config_descriptor_, len, 0);
  req.Complete(ZX_OK, actual);
  return ZX_OK;
}

zx_status_t UsbRootHub::GetStringDescriptor(usb::BorrowedRequest<> req) {
  uint8_t index = static_cast<uint8_t>(req.request()->setup.wValue & 0xff);
  size_t len = static_cast<size_t>(le16toh(req.request()->setup.wLength));

  if (index >= countof(string_descriptor_)) {
    req.Complete(ZX_ERR_OUT_OF_RANGE, 0);
    return ZX_ERR_OUT_OF_RANGE;
  }

  size_t b_len = static_cast<size_t>(string_descriptor_[index]->bLength);
  len = std::min(len, b_len);

  ssize_t actual = req.CopyTo(string_descriptor_[index], len, 0);
  req.Complete(ZX_OK, actual);
  return ZX_OK;
}

zx_status_t UsbRootHub::GetHubDescriptor(usb::BorrowedRequest<> req) {
  uint16_t len = le16toh(req.request()->setup.wLength);
  ZX_ASSERT(len <= sizeof(usb_hub_descriptor_t));
  ssize_t actual = req.CopyTo(&hub_descriptor_, len, 0);
  req.Complete(ZX_OK, actual);
  return ZX_OK;
}

zx_status_t UsbRootHub::GetStatus(usb::BorrowedRequest<> req) {
  uint8_t bm_req_type = req.request()->setup.bmRequestType;
  switch (bm_req_type) {
    case 0xa0:  // See: 11.24.2 (USB 2.0 spec)
      return GetHubStatus(std::move(req));
      break;
    case 0xa3:  // See: 11.24.2 (USB 2.0 spec)
      return GetPortStatus(std::move(req));
      break;
    default:
      zxlogf(ERROR, "unsupported GetStatus() request type: 0x%02x\n", bm_req_type);
      req.Complete(ZX_ERR_NOT_SUPPORTED, 0);
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t UsbRootHub::GetPortStatus(usb::BorrowedRequest<> req) {
  ssize_t actual = req.CopyTo(&port_.status(), sizeof(usb_port_status_t), 0);
  req.Complete(ZX_OK, actual);
  return ZX_OK;
}

zx_status_t UsbRootHub::GetHubStatus(usb::BorrowedRequest<> req) {
  // Currently hub-level status is not supported.
  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  req.Complete(status, 0);
  return status;
}

zx_status_t UsbRootHub::SetConfiguration(usb::BorrowedRequest<> req) {
  uint8_t index = static_cast<uint8_t>(req.request()->setup.wValue & 0xff);
  if (index != 1) {
    zxlogf(ERROR, "unsupported SetConfiguration() index: %d\n", index);
    req.Complete(ZX_ERR_OUT_OF_RANGE, 0);
    return ZX_ERR_OUT_OF_RANGE;
  }

  // This is a no-op for the hub.
  req.Complete(ZX_OK, 0);
  return ZX_OK;
}

zx_status_t UsbRootHub::SetFeature(usb::BorrowedRequest<> req) {
  uint16_t index = le16toh(req.request()->setup.wIndex);
  if (index != 1) {
    zxlogf(ERROR, "unsupported SetFeature() index: %d\n", index);
    req.Complete(ZX_ERR_OUT_OF_RANGE, 0);
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint8_t bm_req_type = req.request()->setup.bmRequestType;
  switch (bm_req_type) {
    case 0x20:  // See: 11.24.2 (USB 2.0 spec)
      return SetHubFeature(std::move(req));
    case 0x23:  // See: 11.24.2 (USB 2.0 spec)
      return SetPortFeature(std::move(req));
    default:
      zxlogf(ERROR, "unsupported SetFeature() request type: 0x%02x\n", bm_req_type);
      req.Complete(ZX_ERR_NOT_SUPPORTED, 0);
      return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t UsbRootHub::SetHubFeature(usb::BorrowedRequest<> req) {
  // Currently hub-level features are not supported.
  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  req.Complete(status, 0);
  return status;
}

zx_status_t UsbRootHub::SetPortFeature(usb::BorrowedRequest<> req) {
  uint16_t feature = static_cast<uint16_t>(letoh16(req.request()->setup.wValue));

  switch (feature) {
    case USB_FEATURE_PORT_RESET:
      port_.Reset();
      break;
    case USB_FEATURE_PORT_SUSPEND:
      port_.Suspend();
      break;
    case USB_FEATURE_PORT_POWER:
      port_.PowerOn();
      break;
    default:
      zxlogf(ERROR, "unsupported SetFeature() selector: 0x%02x\n", feature);
      req.Complete(ZX_ERR_INVALID_ARGS, 0);
      return ZX_ERR_INVALID_ARGS;
  }

  req.Complete(ZX_OK, 0);
  return ZX_OK;
}

int UsbRootHub::EndpointHandlerThread() {
  port_.Wait();
  std::optional<usb::BorrowedRequest<>> req = endpoint_queue_.pop();
  uint8_t status = 1 << 1;  // Signal change to port-1 status, see: 11.12.4 (USB 2.0 spec)
  ssize_t actual = req->CopyTo(&status, sizeof(uint8_t), 0);
  req->Complete(ZX_OK, actual);
  return 0;
}

}  // namespace mt_usb_hci
