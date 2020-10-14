// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_UART_BT_TRANSPORT_UART_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_UART_BT_TRANSPORT_UART_H_

#include <ddk/driver.h>
#include <ddk/protocol/bt/hci.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/bt/hci.h>
#include <ddktl/protocol/serialimpl/async.h>

#include "src/connectivity/bluetooth/hci/lib/bindings.h"

namespace bt_transport_uart {

class BtTransportUart;

using BtTransportUartType = ddk::Device<BtTransportUart, ddk::GetProtocolable, ddk::Unbindable>;

class BtTransportUart : public BtTransportUartType, public ddk::BtHciProtocol<BtTransportUart> {
 public:
  explicit BtTransportUart(zx_device_t* parent) : BtTransportUartType(parent) { worker_ = nullptr; }
  ~BtTransportUart() { ShutdownWorker(); }
  static zx_status_t Bind(void* ctx, zx_device_t* parent);
  zx_status_t Init();

  // ddk::Device methods
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_proto);

  zx_status_t BtHciOpenCommandChannel(zx::channel in);
  zx_status_t BtHciOpenAclDataChannel(zx::channel in);
  zx_status_t BtHciOpenSnoopChannel(zx::channel in);

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(BtTransportUart);

  void ShutdownWorker();
  zx_status_t AddDevice(serial_port_info_t* info);

  bt_hci_transport_handle_t worker_;
};
}  // namespace bt_transport_uart

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_UART_BT_TRANSPORT_UART_H_
