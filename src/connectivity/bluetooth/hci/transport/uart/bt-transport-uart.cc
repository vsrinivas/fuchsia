// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bt-transport-uart.h"

#include <assert.h>
#include <fuchsia/hardware/serial/c/fidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/bt-hci.h>
#include <zircon/status.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/bt/hci.h>
#include <ddk/protocol/serialimpl/async.h>
#include <ddktl/protocol/serialimpl/async.h>
#include <fbl/alloc_checker.h>

#define WORKER_RESPONSE_TIMEOUT_MS 5000

namespace bt_transport_uart {

void BtTransportUart::DdkUnbind(ddk::UnbindTxn txn) {
  zxlogf(INFO, "BtTransportUart::DdkUnbind\n");
  if (worker_) {
    bt_hci_transport_unbind(worker_, WORKER_RESPONSE_TIMEOUT_MS);
  }
  txn.Reply();
}

void BtTransportUart::DdkRelease() {
  zxlogf(INFO, "BtTransportUart::DdkRelease\n");
  ShutdownWorker();
  // FIXME: Leaks BtTransportUart
}

zx_status_t BtTransportUart::DdkGetProtocol(uint32_t proto_id, void* protocol) {
  auto* proto = static_cast<ddk::AnyProtocol*>(protocol);
  if (proto_id != ZX_PROTOCOL_BT_HCI) {
    // Pass this on for drivers to load firmware / initialize
    return device_get_protocol(parent(), proto_id, protocol);
  }

  proto->ctx = this;
  proto->ops = &bt_hci_protocol_ops_;

  return ZX_OK;
}

zx_status_t BtTransportUart::BtHciOpenCommandChannel(zx::channel in) {
  zxlogf(INFO, "BtTransportUart::BtHciOpenCommandChannel");
  return bt_hci_transport_open_command_channel(worker_, in.release(), WORKER_RESPONSE_TIMEOUT_MS);
}

zx_status_t BtTransportUart::BtHciOpenAclDataChannel(zx::channel in) {
  zxlogf(INFO, "BtTransportUart::BtHciOpenAclDataChannel");
  return bt_hci_transport_open_acl_data_channel(worker_, in.release(), WORKER_RESPONSE_TIMEOUT_MS);
}
zx_status_t BtTransportUart::BtHciOpenSnoopChannel(zx::channel in) {
  zxlogf(INFO, "BtTransportUart::BtHciOpenSnoopChannel");
  return bt_hci_transport_open_snoop_channel(worker_, in.release(), WORKER_RESPONSE_TIMEOUT_MS);
}

zx_status_t BtTransportUart::Bind(void* ctx, zx_device_t* parent) {
  zxlogf(INFO, "BtTransportUart::Bind");
  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<BtTransportUart>(new (&ac) BtTransportUart(parent));
  if (!ac.check()) {
    zxlogf(ERROR, "bt-transport-uart: could not allocate BtTransportUart\n");
    return ZX_ERR_NO_MEMORY;
  }

  auto status = dev->Init();
  if (status != ZX_OK) {
    dev->ShutdownWorker();
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();
  return ZX_OK;
}

// Initialize |BtTransportUart| driver and add the device to the device tree.
// This will start the hci |worker_| thread.
zx_status_t BtTransportUart::Init() {
  ddk::SerialImplAsyncProtocolClient serial;
  auto status = ddk::SerialImplAsyncProtocolClient::CreateFromDevice(parent(), &serial);
  if (status == ZX_ERR_NOT_SUPPORTED) {
    zxlogf(ERROR,
           "bt-transport-uart: parent device '%s': does not support serial impl async protocol\n",
           device_get_name(parent()));
    return ZX_ERR_NOT_SUPPORTED;
  } else if (status != ZX_OK) {
    zxlogf(ERROR, "bt-transport-uart: parent device '%s': could not create protocol client %s\n",
           device_get_name(parent()), zx_status_get_string(status));
    return ZX_ERR_NOT_SUPPORTED;
  }

  serial_port_info_t info;
  status = serial.GetInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "bt-transport-uart: serial_get_info failed\n");
    return status;
  }

  if (info.serial_class != fuchsia_hardware_serial_Class_BLUETOOTH_HCI) {
    zxlogf(ERROR, "bt-transport-uart: info.serial_class (%d) != BLUETOOTH_HCI\n",
           info.serial_class);
    return ZX_ERR_INTERNAL;
  }

  status = serial.Enable(true);
  if (status != ZX_OK) {
    zxlogf(ERROR, "bt-transport-uart: serial_enable failed\n");
    return status;
  }

  status = bt_hci_transport_start("uart", &worker_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "bt-transport-uart: bt_hci_transport_start failed\n");
    return status;
  }

  // Allocate serial_proto on the heap so that it can be passed into the Worker thread.
  fbl::AllocChecker ac;
  auto serial_proto =
      std::unique_ptr<serial_impl_async_protocol_t>(new (&ac) serial_impl_async_protocol_t);
  if (!ac.check()) {
    zxlogf(ERROR, "bt-transport-uart: could not allocate serial_impl_asnyc_protocol_t\n");
    return ZX_ERR_NO_MEMORY;
  }

  serial.GetProto(&*serial_proto);

  // Transport_open_transport_uart takes ownership of serial here.
  status = bt_hci_transport_open_uart(worker_, serial_proto.release(), WORKER_RESPONSE_TIMEOUT_MS);
  if (status != ZX_OK) {
    zxlogf(ERROR, "bt-transport-uart: driver_open_transport_hci failed: %s\n",
           zx_status_get_string(status));
    return status;
  }

  return AddDevice(&info);
}

// Add a device to the device tree with the correct bind parameters.
zx_status_t BtTransportUart::AddDevice(serial_port_info_t* info) {
  ddk::DeviceAddArgs args("bt-transport-uart");

  // Assign Bind properties
  zx_device_prop_t props[] = {{}, {}, {}};
  props[0] = {BIND_PROTOCOL, 0, ZX_PROTOCOL_BT_TRANSPORT};
  props[1] = {BIND_SERIAL_VID, 0, info->serial_vid};
  props[2] = {BIND_SERIAL_PID, 0, info->serial_pid};
  args.set_props(props);
  args.set_proto_id(ZX_PROTOCOL_BT_TRANSPORT);

  return DdkAdd(args);
}

// Perform the shutdown procedure associated with the worker thread.
// After this call, |worker_| is null and should not be used.
void BtTransportUart::ShutdownWorker() {
  bt_hci_transport_shutdown(worker_, WORKER_RESPONSE_TIMEOUT_MS);
  worker_ = nullptr;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = BtTransportUart::Bind;
  return ops;
}();

}  // namespace bt_transport_uart

// clang-format off
ZIRCON_DRIVER_BEGIN(bt_transport_uart, bt_transport_uart::driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_SERIAL_IMPL_ASYNC),
    BI_MATCH_IF(EQ, BIND_SERIAL_CLASS, fuchsia_hardware_serial_Class_BLUETOOTH_HCI),
ZIRCON_DRIVER_END(bt_transport_uart)
