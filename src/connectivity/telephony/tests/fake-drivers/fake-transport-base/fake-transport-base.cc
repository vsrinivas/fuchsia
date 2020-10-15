// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-transport-base.h"

#include <lib/async/cpp/task.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstdio>
#include <future>
#include <thread>

#include <ddktl/fidl.h>

namespace fidl_tel_transport = llcpp::fuchsia::hardware::telephony::transport;
namespace fidl_tel_snoop = llcpp::fuchsia::telephony::snoop;

namespace tel_fake {

Device::Device(zx_device_t* device) : parent_(device) {}

void Device::SetChannel(::zx::channel transport,
                        fidl_tel_transport::Qmi::Interface::SetChannelCompleter::Sync& completer) {
  zx_status_t status = ZX_OK;
  zx_status_t set_channel_res = SetChannelToDevice(std::move(transport));
  if (set_channel_res == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(set_channel_res);
  }
  if (status != ZX_OK) {
    goto done;
  }
  if (set_channel_res == ZX_OK) {
    status = SetAsyncWait();
    if (status != ZX_OK) {
      CloseCtrlChannel();
    }
  }
done:
  return;
}

void Device::SetNetwork(bool connected,
                        fidl_tel_transport::Qmi::Interface::SetNetworkCompleter::Sync& completer) {
  SetNetworkStatusToDevice(connected);
  completer.Reply();
}

void Device::SetSnoopChannel(
    ::zx::channel interface,
    fidl_tel_transport::Qmi::Interface::SetSnoopChannelCompleter::Sync& completer) {
  zx_status_t set_snoop_res = SetSnoopChannelToDevice(std::move(interface));
  fidl_tel_transport::Qmi_SetSnoopChannel_Result result;
  if (set_snoop_res == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(set_snoop_res);
  }
}

zx_status_t Device::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fidl_tel_transport::Qmi::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

zx_status_t Device::SetSnoopChannelToDevice(zx::channel channel) {
  zx_status_t result = ZX_OK;
  zx_port_packet_t packet;
  zx_status_t status;
  // Initialize a port to watch whether the other handle of snoop channel has
  // closed
  if (!snoop_channel_port_.is_valid()) {
    status = zx::port::create(0, &snoop_channel_port_);
    if (status != ZX_OK) {
      zxlogf(ERROR,
             "tel-fake-transport: failed to create a port to watch snoop channel: "
             "%s\n",
             zx_status_get_string(status));
      return status;
    }
  } else {
    status = snoop_channel_port_.wait(zx::deadline_after(zx::sec(0)), &packet);
    if (status == ZX_ERR_TIMED_OUT) {
      zxlogf(ERROR, "tel-fake-transport: timed out: %s", zx_status_get_string(status));
    } else if (packet.signal.observed & ZX_CHANNEL_PEER_CLOSED) {
      zxlogf(INFO, "tel-fake-transport: snoop channel peer closed");
      // snoop_channel_.reset();
      (void)snoop_channel_.release();
    }
  }

  if (snoop_channel_.is_valid()) {
    zxlogf(ERROR, "tel-fake-transport: snoop channel already connected");
    result = ZX_ERR_ALREADY_BOUND;
  } else if (channel == ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "tel-fake-transport: get invalid snoop channel handle");
    result = ZX_ERR_BAD_HANDLE;
  } else {
    snoop_channel_.reset(channel.release());
    snoop_channel_.wait_async(snoop_channel_port_, 0, ZX_CHANNEL_PEER_CLOSED, 0);
  }
  return result;
}

zx_status_t Device::CloseCtrlChannel() {
  zx_status_t ret_val = ZX_OK;
  if (ctrl_channel_.is_valid()) {
    ctrl_channel_.reset();
  }
  return ret_val;
}

zx_status_t Device::SetAsyncWait() {
  zx_status_t status = ctrl_channel_.wait_async(ctrl_channel_port_, kChannelMsg,
                                                ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, 0);
  return status;
}

zx_status_t Device::EventLoopCleanup() { return ctrl_channel_port_.cancel(ctrl_channel_, 0); }

void Device::Release() { delete this; }

void Device::Unbind() {
  if (!fake_ctrl_thread_.joinable()) {
    zxlogf(ERROR, "tel-fake-transport: unbind(): thrd unjoinable");
    return;
  }
  zx_port_packet_t packet = {};
  packet.key = kTerminateMsg;
  ctrl_channel_port_.queue(&packet);
  zxlogf(INFO, "tel-fake-transport: unbind(): joining thread");
  fake_ctrl_thread_.join();

  device_unbind_reply(tel_dev_);
  return;
}

zx_status_t Device::GetProtocol(uint32_t proto_id, void* out_proto) {
  if (proto_id != ZX_PROTOCOL_QMI_TRANSPORT) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t Device::SetChannelToDevice(zx::channel transport) {
  zx_status_t result = ZX_OK;
  if (ctrl_channel_.is_valid()) {
    zxlogf(ERROR, "tel-fake-transport: already bound, failing");
    result = ZX_ERR_ALREADY_BOUND;
  } else if (transport == ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "tel-fake-transport: invalid channel handle");
    result = ZX_ERR_BAD_HANDLE;
  } else {
    ctrl_channel_.reset(transport.release());
  }
  return result;
}

zx_status_t Device::SetNetworkStatusToDevice(bool connected) {
  connected_ = connected;
  return ZX_OK;
}

}  // namespace tel_fake
