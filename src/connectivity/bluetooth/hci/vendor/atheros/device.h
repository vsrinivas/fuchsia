// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_VENDOR_ATHEROS_DEVICE_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_VENDOR_ATHEROS_DEVICE_H_

#include <fuchsia/hardware/bluetooth/c/fidl.h>
#include <lib/sync/completion.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/bt/hci.h>
#include <ddk/protocol/usb.h>
#include <ddktl/device.h>
#include <fbl/mutex.h>

#include "src/connectivity/bluetooth/core/bt-host/common/byte_buffer.h"
#include "src/connectivity/bluetooth/core/bt-host/hci/control_packets.h"
#include "src/connectivity/bluetooth/core/bt-host/hci/hci.h"

struct qca_version {
  uint32_t rom_version;
  uint32_t patch_version;
  uint32_t ram_version;
  uint32_t ref_clock;
  uint8_t reserved[4];
} __PACKED;

namespace btatheros {

class Device;

class Device {
 public:
  Device(zx_device_t* device, bt_hci_protocol_t* hci, usb_protocol_t* usb);

  ~Device() = default;

  // Bind the device, invisibly.
  zx_status_t Bind();

  // Load the firmware over usb and complete device initialization.
  // if firmware is loaded, the device will be made visible.
  // otherwise the device will be removed and devhost will
  // unbind.
  zx_status_t LoadFirmware();

  // ddk::Device methods
  void DdkInit();
  void DdkUnbind();
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_proto);
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);

 private:
  static zx_status_t OpenCommandChannel(void* ctx, zx_handle_t channel);
  static zx_status_t OpenAclDataChannel(void* ctx, zx_handle_t channel);
  static zx_status_t OpenSnoopChannel(void* ctx, zx_handle_t channel);

  static constexpr fuchsia_hardware_bluetooth_Hci_ops_t fidl_ops_ = {
      .OpenCommandChannel = OpenCommandChannel,
      .OpenAclDataChannel = OpenAclDataChannel,
      .OpenSnoopChannel = OpenSnoopChannel,
  };

  // Informs the device manager that device initialization has failed,
  // which will unbind the device, and leaves an error on the kernel log
  // prepended with |note|.
  // Returns |status|.
  zx_status_t FailInit(zx_status_t status, const char* note);

  // Load the Qualcomm firmware in RAM
  zx_status_t LoadRAM(const qca_version& ver);

  // Load the Qualcomm firmware in NVM
  zx_status_t LoadNVM(const qca_version& ver);

  // Makes the device visible and leaves |note| on the kernel log.
  // Returns ZX_OK.
  zx_status_t Appear(const char* note);

  // Maps the firmware referenced by |name| into memory.
  // Returns the vmo that the firmware is loaded into or ZX_HANDLE_INVALID if it
  // could not be loaded.
  // Closing this handle will invalidate |fw_addr|, which
  // receives a pointer to the memory.
  // |fw_size| receives the size of the firmware if valid.
  zx_handle_t MapFirmware(const char* name, uintptr_t* fw_addr, size_t* fw_size);

  zx_device_t* parent_;
  zx_device_t* zxdev_;

  fbl::Mutex mutex_;

  // Synchronization for the USB endpoint
  sync_completion_t completion_ __TA_GUARDED(mutex_);

  bt_hci_protocol_t hci_;
  usb_protocol_t usb_;

  size_t parent_req_size_ = 0;
  uint8_t bulk_out_addr_ = 0;

  bool firmware_loaded_ __TA_GUARDED(mutex_);
};

}  // namespace btatheros

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_VENDOR_ATHEROS_DEVICE_H_
