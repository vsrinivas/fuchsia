// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_VENDOR_INTEL_DEVICE_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_VENDOR_INTEL_DEVICE_H_

#include <fuchsia/hardware/bluetooth/c/fidl.h>

#include <optional>

#include <ddk/driver.h>
#include <ddk/protocol/bt/hci.h>
#include <ddktl/device.h>
#include <ddktl/protocol/bt/hci.h>

#include "vendor_hci.h"

namespace btintel {

class Device;

using DeviceType = ddk::Device<Device, ddk::Initializable, ddk::GetProtocolable, ddk::Unbindable,
                               ddk::Messageable>;

class Device : public DeviceType, public ddk::BtHciProtocol<Device, ddk::base_protocol> {
 public:
  Device(zx_device_t* device, bt_hci_protocol_t* hci, bool secure);

  ~Device() = default;

  // Bind the device, invisibly.
  zx_status_t Bind();

  // Load the firmware and complete device initialization.
  // if firmware is loaded, the device will be made visible.
  // otherwise the device will be removed and devhost will
  // unbind.
  // If |secure| is true, use the "secure" firmware method.
  zx_status_t LoadFirmware(ddk::InitTxn init_txn, bool secure);

  // ddk::Device methods
  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_proto);
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);

  zx_status_t BtHciOpenCommandChannel(zx::channel in);
  zx_status_t BtHciOpenAclDataChannel(zx::channel in);
  zx_status_t BtHciOpenSnoopChannel(zx::channel in);

 private:
  static zx_status_t OpenCommandChannel(void* ctx, zx_handle_t in);
  static zx_status_t OpenAclDataChannel(void* ctx, zx_handle_t in);
  static zx_status_t OpenSnoopChannel(void* ctx, zx_handle_t in);

  static constexpr fuchsia_hardware_bluetooth_Hci_ops_t fidl_ops_ = {
      .OpenCommandChannel = OpenCommandChannel,
      .OpenAclDataChannel = OpenAclDataChannel,
      .OpenSnoopChannel = OpenSnoopChannel,
  };

  zx_status_t LoadSecureFirmware(zx::channel* cmd, zx::channel* acl);
  zx_status_t LoadLegacyFirmware(zx::channel* cmd, zx::channel* acl);

  // Informs the device manager that device initialization has failed,
  // which will unbind the device, and leaves an error on the kernel log
  // prepended with |note|.
  // Returns |status|.
  zx_status_t InitFailed(ddk::InitTxn init_txn, zx_status_t status, const char* note);

  // Maps the firmware refrenced by |name| into memory.
  // Returns the vmo that the firmware is loaded into or ZX_HANDLE_INVALID if it
  // could not be loaded.
  // Closing this handle will invalidate |fw_addr|, which
  // receives a pointer to the memory.
  // |fw_size| receives the size of the firmware if valid.
  zx_handle_t MapFirmware(const char* name, uintptr_t* fw_addr, size_t* fw_size);

  ddk::BtHciProtocolClient hci_;
  bool secure_;
  bool firmware_loaded_;
};

}  // namespace btintel

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_VENDOR_INTEL_DEVICE_H_
