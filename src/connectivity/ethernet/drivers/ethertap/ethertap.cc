// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ethertap.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/txn_header.h>
#include <lib/operation/ethernet.h>
#include <stdio.h>
#include <string.h>

#include <memory>

#include <fbl/auto_lock.h>
#include <pretty/hexdump.h>

#include "src/connectivity/ethernet/drivers/ethertap/ethertap-bind.h"

// This macro allows for per-device tracing rather than enabling tracing for the whole driver
#define ethertap_trace(args...)                  \
  do {                                           \
    if (unlikely(options_ & ETHERTAP_OPT_TRACE)) \
      zxlogf(INFO, "ethertap: " args);           \
  } while (0)

#define ETHERTAP_OPT_TRACE (fuchsia_hardware_ethertap::wire::kOptTrace)
#define ETHERTAP_OPT_TRACE_PACKETS (fuchsia_hardware_ethertap::wire::kOptTracePackets)
#define ETHERTAP_OPT_REPORT_PARAM (fuchsia_hardware_ethertap::wire::kOptReportParam)
#define ETHERTAP_OPT_ONLINE (fuchsia_hardware_ethertap::wire::kOptOnline)

namespace eth {

TapCtl::TapCtl(zx_device_t* device) : DeviceType(device) {}

zx_status_t TapCtl::Create(void* ctx, zx_device_t* parent) {
  auto dev = std::unique_ptr<TapCtl>(new TapCtl(parent));
  zx_status_t status = dev->DdkAdd("tapctl");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not add device: %d", __func__, status);
  } else {
    // devmgr owns the memory now
    __UNUSED auto* ptr = dev.release();
  }
  return status;
}

void TapCtl::DdkRelease() { delete this; }

void TapCtl::OpenDevice(OpenDeviceRequestView request, OpenDeviceCompleter::Sync& completer) {
  // copy provided name so we can add a null termination:
  ZX_DEBUG_ASSERT(request->name.size() <= fuchsia_hardware_ethertap::wire::kMaxNameLength);
  char name[fuchsia_hardware_ethertap::wire::kMaxNameLength + 1];
  memcpy(name, request->name.data(), request->name.size());
  name[request->name.size()] = '\0';

  completer.Reply(OpenDeviceInternal(name, request->config, std::move(request->device)));
}

zx_status_t TapCtl::OpenDeviceInternal(
    const char* name, const fuchsia_hardware_ethertap::wire::Config& config,
    fidl::ServerEnd<fuchsia_hardware_ethertap::TapDevice> device) {
  if (config.mtu > fuchsia_hardware_ethertap::wire::kMaxMtu) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto tap =
      std::unique_ptr<eth::TapDevice>(new eth::TapDevice(zxdev(), config, std::move(device)));

  auto status = tap->DdkAdd(name);

  if (status != ZX_OK) {
    zxlogf(ERROR, "tapctl: could not add tap device: %d", status);
  } else {
    // devmgr owns the memory until release is called
    __UNUSED auto ptr = tap.release();
    zxlogf(INFO, "tapctl: created ethertap device '%s'", name);
  }
  return status;
}

int tap_device_thread(void* arg) {
  TapDevice* device = reinterpret_cast<TapDevice*>(arg);
  return device->Thread();
}

#define TAP_SHUTDOWN ZX_USER_SIGNAL_7

TapDevice::TapDevice(zx_device_t* device, const fuchsia_hardware_ethertap::wire::Config& config,
                     fidl::ServerEnd<fuchsia_hardware_ethertap::TapDevice> server_end)
    : ddk::Device<TapDevice, ddk::Unbindable>(device),
      options_(config.options),
      features_(config.features | ETHERNET_FEATURE_SYNTH),
      mtu_(config.mtu),
      online_((config.options & ETHERTAP_OPT_ONLINE) != 0),
      server_end_(std::move(server_end)) {
  ZX_DEBUG_ASSERT(server_end_.is_valid());
  memcpy(mac_, config.mac.octets.data(), 6);

  int ret = thrd_create_with_name(&thread_, tap_device_thread, reinterpret_cast<void*>(this),
                                  "ethertap-thread");
  ZX_DEBUG_ASSERT(ret == thrd_success);
}

void TapDevice::DdkRelease() {
  ethertap_trace("DdkRelease\n");
  int ret = thrd_join(thread_, nullptr);
  ZX_DEBUG_ASSERT(ret == thrd_success);
  delete this;
}

void TapDevice::DdkUnbind(ddk::UnbindTxn txn) {
  ethertap_trace("DdkUnbind\n");
  fbl::AutoLock lock(&lock_);
  if (dead_) {
    // If the worker thread is already dead, we can reply to the unbind immediately.
    txn.Reply();
    return;
  }
  unbind_txn_ = std::move(txn);
  zx_status_t status = server_end_.channel().signal(0, TAP_SHUTDOWN);
  ZX_DEBUG_ASSERT(status == ZX_OK);
  // When the thread exits after the channel is closed, it will reply to the unbind txn.
}

zx_status_t TapDevice::EthernetImplQuery(uint32_t options, ethernet_info_t* info) {
  memset(info, 0, sizeof(*info));
  info->features = features_;
  info->mtu = mtu_;
  memcpy(info->mac, mac_, 6);
  info->netbuf_size = eth::BorrowedOperation<>::OperationSize(sizeof(ethernet_netbuf_t));
  return ZX_OK;
}

void TapDevice::EthernetImplStop() {
  ethertap_trace("EthernetImplStop\n");
  fbl::AutoLock lock(&lock_);
  ethernet_client_.clear();
}

zx_status_t TapDevice::EthernetImplStart(const ethernet_ifc_protocol_t* ifc) {
  ethertap_trace("EthernetImplStart\n");
  fbl::AutoLock lock(&lock_);
  if (ethernet_client_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  } else {
    ethernet_client_ = ddk::EthernetIfcProtocolClient(ifc);
    ethernet_client_.Status(online_ ? ETHERNET_STATUS_ONLINE : 0u);
  }
  return ZX_OK;
}

void TapDevice::EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                                    ethernet_impl_queue_tx_callback completion_cb, void* cookie) {
  eth::BorrowedOperation<> op(netbuf, completion_cb, cookie, sizeof(ethernet_netbuf_t));
  fbl::AutoLock lock(&lock_);
  if (dead_) {
    op.Complete(ZX_ERR_PEER_CLOSED);
    return;
  } else if (!online_) {
    ethertap_trace("dropping packet, device offline\n");
    op.Complete(ZX_ERR_UNAVAILABLE);
    return;
  }

  size_t length = op.operation()->data_size;
  ZX_DEBUG_ASSERT(length <= mtu_);

  if (unlikely(options_ & ETHERTAP_OPT_TRACE_PACKETS)) {
    ethertap_trace("sending %zu bytes\n", length);
    hexdump8_ex(op.operation()->data_buffer, length, 0);
  }

  // TODO(fxbug.dev/101890): Remove the const-cast.
  auto result = fidl::WireSendEvent(server_end_)
                    ->OnFrame(fidl::VectorView<uint8_t>::FromExternal(
                        const_cast<uint8_t*>(op.operation()->data_buffer), length));
  if (result.status() != ZX_OK) {
    zxlogf(WARNING, "ethertap: EthernetImplQueueTx error writing: %d", result.status());
  }
  // returning ZX_ERR_SHOULD_WAIT indicates that we will call complete_tx(), which we will not
  op.Complete(result.status() == ZX_ERR_SHOULD_WAIT ? ZX_ERR_UNAVAILABLE : result.status());
}

zx_status_t TapDevice::EthernetImplSetParam(uint32_t param, int32_t value, const uint8_t* data,
                                            size_t data_size) {
  fbl::AutoLock lock(&lock_);
  if (!(options_ & ETHERTAP_OPT_REPORT_PARAM) || dead_) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  fidl::Arena arena;
  fidl::VectorView<uint8_t> event_data;

  switch (param) {
    case ETHERNET_SETPARAM_MULTICAST_FILTER:
      if (value == ETHERNET_MULTICAST_FILTER_OVERFLOW) {
        break;
      } else {
        // Send the final byte of each address, sorted lowest-to-highest.
        auto size = static_cast<uint32_t>(value) < fuchsia_hardware_ethertap::wire::kMaxParamData
                        ? static_cast<uint32_t>(value)
                        : fuchsia_hardware_ethertap::wire::kMaxParamData;
        event_data = fidl::VectorView<uint8_t>(arena, size);

        uint32_t i;
        for (i = 0; i < size; i++) {
          event_data[i] = static_cast<const uint8_t*>(data)[i * ETH_MAC_SIZE + 5];
        }
        qsort(event_data.begin(), size, 1, [](const void* ap, const void* bp) {
          int a = *static_cast<const uint8_t*>(ap);
          int b = *static_cast<const uint8_t*>(bp);
          return a < b ? -1 : (a > b ? 1 : 0);
        });
      }
      break;
    default:
      break;
  }

  auto result = fidl::WireSendEvent(server_end_)->OnReportParams(param, value, event_data);
  if (!result.ok() && !result.is_peer_closed()) {
    // A failure of sending the event data is not a simulated failure of hardware under test,
    // so log it but don't report failure on the SetParam attempt.

    zxlogf(ERROR, "ethertap: TapDevice sending OnReportParams failed: %s",
           result.FormatDescription().c_str());
  }

  return ZX_OK;
}

void TapDevice::EthernetImplGetBti(zx::bti* bti) { bti->reset(); }

void TapDevice::UpdateLinkStatus(bool online) {
  bool was_online = online_;

  if (online) {
    ethertap_trace("online asserted\n");
    online_ = true;
  } else {
    ethertap_trace("offline asserted\n");
    online_ = false;
  }

  if (was_online != online_) {
    fbl::AutoLock lock(&lock_);
    if (ethernet_client_.is_valid()) {
      ethernet_client_.Status(online_ ? ETHERNET_STATUS_ONLINE : 0u);
    }
    ethertap_trace("device '%s' is now %s\n", name(), online_ ? "online" : "offline");
  }
}

zx_status_t TapDevice::Recv(const uint8_t* buffer, size_t length) {
  fbl::AutoLock lock(&lock_);

  if (!online_) {
    ethertap_trace("attempted to push bytes to an offline device\n");
    return ZX_OK;
  }

  if (unlikely(options_ & ETHERTAP_OPT_TRACE_PACKETS)) {
    ethertap_trace("received %lu bytes\n", length);
    hexdump8_ex(buffer, length, 0);
  }

  if (ethernet_client_.is_valid()) {
    ethernet_client_.Recv(buffer, length, 0u);
  }
  return ZX_OK;
}

class TapDeviceTransaction : public fidl::Transaction {
 public:
  TapDeviceTransaction(TapDevice* device, zx_txid_t txid) : device_(device), txid_(txid) {}

  std::unique_ptr<Transaction> TakeOwnership() override { ZX_PANIC("unimplemented"); }

  zx_status_t Reply(fidl::OutgoingMessage* message,
                    fidl::WriteOptions write_options = {}) override {
    return device_->Reply(txid_, message);
  }

  void Close(zx_status_t epitaph) override { ZX_PANIC("unimplemented"); }

 private:
  TapDevice* device_;
  zx_txid_t txid_;
};

zx_status_t TapDevice::Reply(zx_txid_t txid, fidl::OutgoingMessage* msg) {
  msg->set_txid(txid);
  msg->Write(server_end_.channel());
  return msg->status();
}

class TapDeviceServer : public fidl::WireServer<fuchsia_hardware_ethertap::TapDevice> {
 public:
  TapDeviceServer(TapDevice* tap_device) : tap_device_(tap_device) {}

  void WriteFrame(WriteFrameRequestView request, WriteFrameCompleter::Sync& completer) override {
    tap_device_->Recv(request->data.data(), request->data.count());
  }

  void SetOnline(SetOnlineRequestView request, SetOnlineCompleter::Sync& completer) override {
    tap_device_->UpdateLinkStatus(request->online);
  }

 private:
  TapDevice* tap_device_;
};

int TapDevice::Thread() {
  ethertap_trace("starting main thread\n");
  zx_signals_t pending;
  const uint32_t buff_size = 2 * mtu_;
  constexpr uint32_t handle_count = 8;
  std::unique_ptr<uint8_t[]> data_buff(new uint8_t[buff_size]);
  zx_handle_t handles_buff[handle_count];
  fidl_channel_handle_metadata_t handle_metadata_buff[handle_count];

  fidl_incoming_msg_t msg = {
      .bytes = data_buff.get(),
      .handles = handles_buff,
      .handle_metadata = reinterpret_cast<fidl_handle_metadata_t*>(handle_metadata_buff),
      .num_bytes = buff_size,
      .num_handles = handle_count,
  };

  TapDeviceServer server(this);

  zx_status_t status = ZX_OK;
  const zx_signals_t wait = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED | TAP_SHUTDOWN;
  while (true) {
    status = server_end_.channel().wait_one(wait, zx::time::infinite(), &pending);
    if (status != ZX_OK) {
      ethertap_trace("error waiting on channel: %d\n", status);
      break;
    }

    if (pending & ZX_CHANNEL_READABLE) {
      zx_handle_info_t handle_infos[handle_count];
      status = server_end_.channel().read_etc(0, msg.bytes, handle_infos, buff_size, handle_count,
                                              &msg.num_bytes, &msg.num_handles);
      if (status != ZX_OK) {
        ethertap_trace("message read failed: %d\n", status);
        break;
      }
      for (uint32_t i = 0; i < msg.num_handles; i++) {
        handles_buff[i] = handle_infos[i].handle;
        handle_metadata_buff[i] = fidl_channel_handle_metadata_t{
            .obj_type = handle_infos[i].type,
            .rights = handle_infos[i].rights,
        };
      }

      TapDeviceTransaction txn(this,
                               reinterpret_cast<const fidl_message_header_t*>(msg.bytes)->txid);
      fidl::WireDispatch(&server, fidl::IncomingMessage::FromEncodedCMessage(&msg), &txn);
    }
    if (pending & ZX_CHANNEL_PEER_CLOSED) {
      ethertap_trace("channel closed (peer)\n");
      break;
    }
    if (pending & TAP_SHUTDOWN) {
      ethertap_trace("channel closed (self)\n");
      break;
    }
  }
  {
    fbl::AutoLock lock(&lock_);
    dead_ = true;
    zxlogf(INFO, "ethertap: device '%s' destroyed", name());
    server_end_.channel().reset();
    // Check if the unbind hook is expecting a response.
    if (unbind_txn_) {
      unbind_txn_->Reply();
    } else {
      // Schedule unbinding to begin.
      DdkAsyncRemove();
    }
  }

  return static_cast<int>(status);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = TapCtl::Create;
  return ops;
}();

}  // namespace eth

ZIRCON_DRIVER(tapctl, eth::driver_ops, "zircon", "0.1");
