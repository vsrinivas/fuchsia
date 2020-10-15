// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fuchsia/hardware/bluetooth/c/fidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/status.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/bt/hci.h>

typedef struct {
  zx_device_t* dev;
  zx_device_t* transport_dev;

  bt_hci_protocol_t hci;
} passthrough_t;

static zx_status_t bt_hci_passthrough_get_protocol(void* ctx, uint32_t proto_id, void* out_proto) {
  if (proto_id != ZX_PROTOCOL_BT_HCI) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  passthrough_t* passthrough = ctx;
  bt_hci_protocol_t* hci_proto = out_proto;

  // Forward the underlying bt-transport ops.
  hci_proto->ops = passthrough->hci.ops;
  hci_proto->ctx = passthrough->hci.ctx;

  return ZX_OK;
}

static void bt_hci_passthrough_unbind(void* ctx) {
  passthrough_t* passthrough = ctx;

  device_unbind_reply(passthrough->dev);
}

static void bt_hci_passthrough_release(void* ctx) { free(ctx); }

zx_status_t fidl_bt_hci_open_command_channel(void* ctx, zx_handle_t channel) {
  passthrough_t* hci = ctx;
  return bt_hci_open_command_channel(&hci->hci, channel);
}

zx_status_t fidl_bt_hci_open_acl_data_channel(void* ctx, zx_handle_t channel) {
  passthrough_t* hci = ctx;
  return bt_hci_open_acl_data_channel(&hci->hci, channel);
}

zx_status_t fidl_bt_hci_open_snoop_channel(void* ctx, zx_handle_t channel) {
  passthrough_t* hci = ctx;
  return bt_hci_open_snoop_channel(&hci->hci, channel);
}

const fuchsia_hardware_bluetooth_Hci_ops_t fidl_ops = {
    .OpenCommandChannel = fidl_bt_hci_open_command_channel,
    .OpenAclDataChannel = fidl_bt_hci_open_acl_data_channel,
    .OpenSnoopChannel = fidl_bt_hci_open_snoop_channel,
};

static zx_status_t fuchsia_bt_hci_message_instance(void* ctx, fidl_incoming_msg_t* msg,
                                                   fidl_txn_t* txn) {
  return fuchsia_hardware_bluetooth_Hci_dispatch(ctx, txn, msg, &fidl_ops);
}

static zx_protocol_device_t passthrough_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = bt_hci_passthrough_get_protocol,
    .message = fuchsia_bt_hci_message_instance,
    .unbind = bt_hci_passthrough_unbind,
    .release = bt_hci_passthrough_release,
};

static zx_status_t bt_hci_passthrough_bind(void* ctx, zx_device_t* device) {
  printf("bt_hci_passthrough_bind: starting\n");
  passthrough_t* passthrough = calloc(1, sizeof(passthrough_t));
  if (!passthrough) {
    printf("bt_hci_passthrough_bind: not enough memory\n");
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = device_get_protocol(device, ZX_PROTOCOL_BT_HCI, &passthrough->hci);
  if (status != ZX_OK) {
    printf("bt_hci_passthrough_bind: failed protocol: %s\n", zx_status_get_string(status));
    return status;
  }

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "bt_hci_passthrough",
      .ctx = passthrough,
      .ops = &passthrough_device_proto,
      .proto_id = ZX_PROTOCOL_BT_HCI,
  };

  passthrough->transport_dev = device;

  status = device_add(device, &args, &passthrough->dev);
  if (status == ZX_OK) {
    return status;
  }

  printf("bt_hci_passthrough_bind failed: %s\n", zx_status_get_string(status));
  bt_hci_passthrough_release(passthrough);
  return status;
}

static zx_driver_ops_t bt_hci_passthrough_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = bt_hci_passthrough_bind,
};

// This should be the last driver queried, so we match any transport.
// clang-format off
ZIRCON_DRIVER_BEGIN(bt_hci_passthrough, bt_hci_passthrough_driver_ops, "fuchsia", "*0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_BT_TRANSPORT),
ZIRCON_DRIVER_END(bt_hci_passthrough)
