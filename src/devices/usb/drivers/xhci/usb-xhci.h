// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/device-protocol/pdev.h>

#include <memory>

#include <ddktl/device.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/pci.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/usb/hci.h>
#include <fbl/array.h>

#include "xhci.h"

namespace usb_xhci {

class UsbXhci;
using UsbXhciType = ddk::Device<UsbXhci, ddk::SuspendableNew, ddk::UnbindableNew>;

// This is the main class for the USB XHCI host controller driver.
class UsbXhci : public UsbXhciType, public ddk::UsbHciProtocol<UsbXhci, ddk::base_protocol> {
 public:
  explicit UsbXhci(zx_device_t* parent)
      : UsbXhciType(parent), pci_(parent), pdev_(parent), composite_(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkSuspendNew(ddk::SuspendTxn txn);
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

  // USB HCI protocol implementation.
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

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(UsbXhci);

  struct Completer {
    xhci_t* xhci;
    uint32_t interrupter;
    bool high_priority;
  };

  int StartThread();
  static int CompleterThread(void* arg);
  zx_status_t FinishBind();
  zx_status_t InitPci();
  zx_status_t InitPdev();
  zx_status_t Init();

  fbl::Array<Completer> completers_;

  // Pointer to C struct that represents most of the driver.
  std::unique_ptr<xhci_t> xhci_;

  ddk::PciProtocolClient pci_;
  ddk::PDev pdev_;
  ddk::CompositeProtocolClient composite_;
};

}  // namespace usb_xhci
