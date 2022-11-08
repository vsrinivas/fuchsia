// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_VIRTUAL_LOOPBACK_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_VIRTUAL_LOOPBACK_H_

#include <fuchsia/hardware/bt/hci/cpp/banjo.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ddktl/device.h>

namespace bt_hci_virtual {

// A driver that implements a ZX_PROTOCOL_BT_HCI device.
class LoopbackDevice;
using LoopbackDeviceType = ddk::Device<LoopbackDevice, ddk::Unbindable>;

class LoopbackDevice : public LoopbackDeviceType, public ddk::BtHciProtocol<LoopbackDevice> {
 public:
  explicit LoopbackDevice(zx_device_t* parent) : LoopbackDeviceType(parent){};

  // ddk::BtHciProtocol mixins:
  zx_status_t BtHciOpenCommandChannel(zx::channel channel);
  zx_status_t BtHciOpenAclDataChannel(zx::channel channel);
  zx_status_t BtHciOpenScoChannel(zx::channel channel);
  void BtHciConfigureSco(sco_coding_format_t coding_format, sco_encoding_t encoding,
                         sco_sample_rate_t sample_rate, bt_hci_configure_sco_callback callback,
                         void* cookie);
  void BtHciResetSco(bt_hci_reset_sco_callback callback, void* cookie);
  zx_status_t BtHciOpenSnoopChannel(zx::channel channel);

  // DDK Mixins
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  // Bind this device to this underlying UART channel.
  zx_status_t Bind(zx_handle_t channel);

 private:
};

}  // namespace bt_hci_virtual

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_VIRTUAL_LOOPBACK_H_
