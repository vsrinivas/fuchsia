// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <fuchsia/hardware/ethernet/cpp/banjo.h>
#include <fuchsia/hardware/wlan/phyinfo/c/banjo.h>
#include <fuchsia/hardware/wlan/softmac/cpp/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/ieee80211/c/banjo.h>
#include <fuchsia/wlan/internal/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

#include <wlan/common/channel.h>
#include <wlan/common/logging.h>
#include <wlan/mlme/ap/ap_mlme.h>
#include <wlan/mlme/client/client_mlme.h>
#include <wlan/mlme/validate_frame.h>
#include <wlan/mlme/wlan.h>

#include "probe_sequence.h"

namespace wlan {

#define DEV(c) static_cast<Device*>(c)
static zx_protocol_device_t eth_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->EthUnbind(); },
    .release = [](void* ctx) { DEV(ctx)->EthRelease(); },
};

static ethernet_impl_protocol_ops_t ethernet_impl_ops = {
    .query = [](void* ctx, uint32_t options, ethernet_info_t* info) -> zx_status_t {
      return DEV(ctx)->EthernetImplQuery(options, info);
    },
    .stop = [](void* ctx) { DEV(ctx)->EthernetImplStop(); },
    .start = [](void* ctx, const ethernet_ifc_protocol_t* ifc) -> zx_status_t {
      return DEV(ctx)->EthernetImplStart(ifc);
    },
    .queue_tx =
        [](void* ctx, uint32_t options, ethernet_netbuf_t* netbuf,
           ethernet_impl_queue_tx_callback completion_cb,
           void* cookie) { DEV(ctx)->EthernetImplQueueTx(options, netbuf, completion_cb, cookie); },
    .set_param = [](void* ctx, uint32_t param, int32_t value, const uint8_t* data, size_t data_size)
        -> zx_status_t { return DEV(ctx)->EthernetImplSetParam(param, value, data, data_size); },
};
#undef DEV

Device::Device(zx_device_t* device, wlan_softmac_protocol_t wlan_softmac_proto)
    : parent_(device), wlan_softmac_proxy_(&wlan_softmac_proto) {
  infof("Creating a new WLAN device.\n");
  debugfn();
  state_ = fbl::AdoptRef(new DeviceState);
}

Device::~Device() { debugfn(); }

// Disable thread safety analysis, as this is a part of device initialization.
// All thread-unsafe work should occur before multiple threads are possible
// (e.g., before MainLoop is started and before DdkAdd() is called), or locks
// should be held.
zx_status_t Device::Bind() __TA_NO_THREAD_SAFETY_ANALYSIS {
  debugfn();
  infof("Binding our new WLAN device.\n");

  zx_status_t status = wlan_softmac_proxy_.Query(&wlan_softmac_info_);
  if (status != ZX_OK) {
    errorf("could not query wlan-softmac device: %d\n", status);
    return status;
  }

  wlan_softmac_proxy_.QueryDiscoverySupport(&discovery_support_);
  wlan_softmac_proxy_.QueryMacSublayerSupport(&mac_sublayer_support_);
  wlan_softmac_proxy_.QuerySecuritySupport(&security_support_);
  wlan_softmac_proxy_.QuerySpectrumManagementSupport(&spectrum_management_support_);

  state_->set_address(common::MacAddr(wlan_softmac_info_.sta_addr));

  switch (wlan_softmac_info_.mac_role) {
    case WLAN_MAC_ROLE_CLIENT:
      infof("Initialize a client MLME.\n");
      mlme_.reset(new ClientMlme(this, ClientMlmeDefaultConfig()));
      break;
    case WLAN_MAC_ROLE_AP:
      infof("Initialize an AP MLME.\n");
      mlme_.reset(new ApMlme(this));
      break;
    // TODO(fxbug.dev/44485): Add support for WLAN_MAC_ROLE_MESH.
    default:
      errorf("unsupported MAC role: %u\n", wlan_softmac_info_.mac_role);
      return ZX_ERR_NOT_SUPPORTED;
  }
  ZX_DEBUG_ASSERT(mlme_ != nullptr);
  status = mlme_->Init();
  if (status != ZX_OK) {
    errorf("could not initialize MLME: %d\n", status);
    return status;
  }

  status = AddEthDevice();
  if (status != ZX_OK) {
    errorf("could not add eth device: %s\n", zx_status_get_string(status));
    mlme_->StopMainLoop();
    return status;
  }

  debugf("device added\n");
  return ZX_OK;
}

zx_status_t Device::AddEthDevice() {
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "wlan-ethernet";
  args.ctx = this;
  args.ops = &eth_device_ops;
  args.proto_id = ZX_PROTOCOL_ETHERNET_IMPL;
  args.proto_ops = &ethernet_impl_ops;
  return device_add(parent_, &args, &ethdev_);
}

std::unique_ptr<Packet> Device::PreparePacket(const void* data, size_t length, Packet::Peer peer) {
  std::unique_ptr<Buffer> buffer = GetBuffer(length);
  if (buffer == nullptr) {
    errorf("could not get buffer for packet of length %zu\n", length);
    return nullptr;
  }

  auto packet = std::unique_ptr<Packet>(new Packet(std::move(buffer), length));
  packet->set_peer(peer);
  zx_status_t status = packet->CopyFrom(data, length, 0);
  if (status != ZX_OK) {
    errorf("could not copy to packet: %d\n", status);
    return nullptr;
  }
  return packet;
}

// Our Device instance is leaked deliberately during the driver bind procedure, so we
// manually take ownership here.
void Device::DestroySelf() { delete this; }

void Device::ShutdownMainLoop() {
  if (mlme_main_loop_dead_) {
    errorf("ShutdownMainLoop called while main loop was not running");
    return;
  }
  mlme_->StopMainLoop();
  mlme_main_loop_dead_ = true;
}

// ddk ethernet_impl_protocol_ops methods

void Device::EthUnbind() {
  debugfn();
  ShutdownMainLoop();
  wlan_softmac_proxy_.Stop();
  device_async_remove(ethdev_);
}

void Device::EthRelease() {
  debugfn();
  // The lifetime of this device is managed by the parent ethernet device, but we don't
  // have a mechanism to make this explicit. EthUnbind is already called at this point,
  // so it's safe to clean up our memory usage.
  DestroySelf();
}

zx_status_t Device::EthernetImplQuery(uint32_t options, ethernet_info_t* info) {
  debugfn();
  if (info == nullptr)
    return ZX_ERR_INVALID_ARGS;

  memset(info, 0, sizeof(*info));
  memcpy(info->mac, wlan_softmac_info_.sta_addr, ETH_MAC_SIZE);
  info->features = ETHERNET_FEATURE_WLAN;
  mac_sublayer_support_t mac_sublayer;
  wlan_softmac_proxy_.QueryMacSublayerSupport(&mac_sublayer);
  if (mac_sublayer.device.is_synthetic) {
    info->features |= ETHERNET_FEATURE_SYNTH;
  }
  info->mtu = 1500;
  info->netbuf_size = eth::BorrowedOperation<>::OperationSize(sizeof(ethernet_netbuf_t));

  return ZX_OK;
}

zx_status_t Device::EthernetImplStart(const ethernet_ifc_protocol_t* ifc) {
  debugfn();
  ZX_DEBUG_ASSERT(ifc != nullptr);

  std::lock_guard<std::mutex> lock(ethernet_proxy_lock_);
  if (ethernet_proxy_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  ethernet_proxy_ = ddk::EthernetIfcProtocolClient(ifc);
  return ZX_OK;
}

void Device::EthernetImplStop() {
  debugfn();

  std::lock_guard<std::mutex> lock(ethernet_proxy_lock_);
  if (!ethernet_proxy_.is_valid()) {
    warnf("ethmac not started\n");
  }
  ethernet_proxy_.clear();
}

void Device::EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                                 ethernet_impl_queue_tx_callback completion_cb, void* cookie) {
  eth::BorrowedOperation<> op(netbuf, completion_cb, cookie, sizeof(ethernet_netbuf_t));
  // no debugfn() because it's too noisy
  auto packet = PreparePacket(op.operation()->data_buffer, op.operation()->data_size,
                              Packet::Peer::kEthernet);
  if (packet == nullptr) {
    warnf("could not prepare Ethernet packet with len %zu\n", netbuf->data_size);
    op.Complete(ZX_ERR_NO_RESOURCES);
    return;
  }

  // Forward the packet straight into Rust MLME.
  auto status = mlme_->QueueEthFrameTx(std::move(packet));
  if (status != ZX_OK) {
    warnf("could not queue Ethernet packet err=%s\n", zx_status_get_string(status));
    ZX_DEBUG_ASSERT(status != ZX_ERR_SHOULD_WAIT);
  }
  op.Complete(status);
}

zx_status_t Device::EthernetImplSetParam(uint32_t param, int32_t value, const void* data,
                                         size_t data_size) {
  debugfn();

  zx_status_t status = ZX_ERR_NOT_SUPPORTED;

  switch (param) {
    case ETHERNET_SETPARAM_PROMISC:
      // See fxbug.dev/28881: In short, the bridge mode doesn't require WLAN
      // promiscuous mode enabled.
      //               So we give a warning and return OK here to continue the
      //               bridging.
      // TODO(fxbug.dev/29113): To implement the real promiscuous mode.
      if (value == 1) {  // Only warn when enabling.
        warnf("WLAN promiscuous not supported yet. see fxbug.dev/29113\n");
      }
      status = ZX_OK;
      break;
  }

  return status;
}

zx_status_t Device::Start(const rust_wlan_softmac_ifc_protocol_copy_t* ifc,
                          zx::channel* out_sme_channel) {
  debugf("Start");

  // We manually populate the protocol ops here so that we can verify at compile time that our rust
  // bindings have the expected parameters.
  wlan_softmac_ifc_protocol_ops_.reset(new wlan_softmac_ifc_protocol_ops_t{
      .status = ifc->ops->status,
      .recv = ifc->ops->recv,
      .complete_tx = ifc->ops->complete_tx,
      .report_tx_status = ifc->ops->report_tx_status,
      .scan_complete = ifc->ops->scan_complete,
  });

  return wlan_softmac_proxy_.Start(ifc->ctx, wlan_softmac_ifc_protocol_ops_.get(), out_sme_channel);
}

zx_status_t Device::DeliverEthernet(cpp20::span<const uint8_t> eth_frame) {
  if (eth_frame.size() > ETH_FRAME_MAX_SIZE) {
    errorf("Attempted to deliver an ethernet frame of invalid length: %zu\n", eth_frame.size());
    return ZX_ERR_INVALID_ARGS;
  }

  std::lock_guard<std::mutex> lock(ethernet_proxy_lock_);
  if (ethernet_proxy_.is_valid()) {
    ethernet_proxy_.Recv(eth_frame.data(), eth_frame.size(), 0u);
  }
  return ZX_OK;
}

zx_status_t Device::QueueTx(std::unique_ptr<Packet> packet, wlan_tx_info_t tx_info) {
  ZX_DEBUG_ASSERT(packet->len() <= std::numeric_limits<uint16_t>::max());
  packet->CopyCtrlFrom(tx_info);
  wlan_tx_packet_t tx_pkt = packet->AsWlanTxPacket();
  bool enqueue_pending = false;
  auto status = wlan_softmac_proxy_.QueueTx(&tx_pkt, &enqueue_pending);
  // TODO(fxbug.dev/85924): Remove this once we implement WlanSoftmacCompleteTx
  // and allow wlan-softmac drivers to complete transmits asynchronously.
  ZX_DEBUG_ASSERT(!enqueue_pending);

  return status;
}

// Disable thread safety analysis, since these methods are called through an
// interface from an object that we know is holding the lock. So taking the lock
// would be wrong, but there's no way to convince the compiler that the lock is
// held.

// TODO(tkilbourn): figure out how to make sure we have the lock for accessing
// dispatcher_.
zx_status_t Device::SetChannel(wlan_channel_t channel) __TA_NO_THREAD_SAFETY_ANALYSIS {
  // TODO(porce): Implement == operator for wlan_channel_t, or an equality test
  // function.

  char buf[80];
  snprintf(buf, sizeof(buf), "channel set: from %s to %s",
           common::ChanStr(state_->channel()).c_str(), common::ChanStr(channel).c_str());

  zx_status_t status = wlan_softmac_proxy_.SetChannel(&channel);
  if (status != ZX_OK) {
    errorf("%s change failed (status %d)\n", buf, status);
    return status;
  }

  state_->set_channel(channel);

  verbosef("%s succeeded\n", buf);
  return ZX_OK;
}

zx_status_t Device::SetStatus(uint32_t status) {
  std::lock_guard<std::mutex> lock(ethernet_proxy_lock_);
  if (ethernet_proxy_.is_valid()) {
    ethernet_proxy_.Status(status);
  }
  return ZX_OK;
}

zx_status_t Device::ConfigureBss(bss_config_t* cfg) {
  return wlan_softmac_proxy_.ConfigureBss(cfg);
}

zx_status_t Device::EnableBeaconing(wlan_bcn_config_t* bcn_cfg) {
  if (bcn_cfg != nullptr) {
    ZX_DEBUG_ASSERT(
        ValidateFrame("Malformed beacon template",
                      {reinterpret_cast<const uint8_t*>(bcn_cfg->packet_template.mac_frame_buffer),
                       bcn_cfg->packet_template.mac_frame_size}));
  }
  return wlan_softmac_proxy_.EnableBeaconing(bcn_cfg);
}

zx_status_t Device::ConfigureBeacon(std::unique_ptr<Packet> beacon) {
  ZX_DEBUG_ASSERT(beacon.get() != nullptr);
  if (beacon.get() == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  ZX_DEBUG_ASSERT(ValidateFrame("Malformed beacon template", {beacon->data(), beacon->size()}));

  wlan_tx_packet_t tx_packet = beacon->AsWlanTxPacket();
  return wlan_softmac_proxy_.ConfigureBeacon(&tx_packet);
}

zx_status_t Device::SetKey(wlan_key_config_t* key_config) {
  return wlan_softmac_proxy_.SetKey(key_config);
}

zx_status_t Device::StartPassiveScan(const wlan_softmac_passive_scan_args_t* passive_scan_args,
                                     uint64_t* out_scan_id) {
  return wlan_softmac_proxy_.StartPassiveScan(passive_scan_args, out_scan_id);
}

zx_status_t Device::StartActiveScan(const wlan_softmac_active_scan_args_t* active_scan_args,
                                    uint64_t* out_scan_id) {
  return wlan_softmac_proxy_.StartActiveScan(active_scan_args, out_scan_id);
}

zx_status_t Device::ConfigureAssoc(wlan_assoc_ctx_t* assoc_ctx) {
  return wlan_softmac_proxy_.ConfigureAssoc(assoc_ctx);
}

zx_status_t Device::ClearAssoc(const uint8_t peer_addr[fuchsia_wlan_ieee80211_MAC_ADDR_LEN]) {
  return wlan_softmac_proxy_.ClearAssoc(peer_addr);
}

fbl::RefPtr<DeviceState> Device::GetState() { return state_; }

const wlan_softmac_info_t& Device::GetWlanSoftmacInfo() const { return wlan_softmac_info_; }

const discovery_support_t& Device::GetDiscoverySupport() const { return discovery_support_; }

const mac_sublayer_support_t& Device::GetMacSublayerSupport() const {
  return mac_sublayer_support_;
}

const security_support_t& Device::GetSecuritySupport() const { return security_support_; }

const spectrum_management_support_t& Device::GetSpectrumManagementSupport() const {
  return spectrum_management_support_;
}
}  // namespace wlan
