// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <fuchsia/hardware/ethernet/cpp/banjo.h>
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

#include "convert.h"
#include "fidl/fuchsia.wlan.common/cpp/wire_types.h"
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

Device::Device(zx_device_t* device, fdf::ClientEnd<fuchsia_wlan_softmac::WlanSoftmac> client)
    : ddk::Device<Device, ddk::Unbindable>(device), parent_(device) {
  infof("Creating a new WLAN device.");
  debugfn();
  state_ = fbl::AdoptRef(new DeviceState);

  // Create a dispatcher to wait on the runtime channel.
  auto dispatcher = fdf::Dispatcher::Create(0, "wlansoftmacifc_server", [&](fdf_dispatcher_t*) {
    if (unbind_txn_ != std::nullopt)
      unbind_txn_->Reply();
    else
      device_unbind_reply(ethdev_);
  });

  if (dispatcher.is_error()) {
    ZX_ASSERT_MSG(false, "Creating server dispatcher error: %s",
                  zx_status_get_string(dispatcher.status_value()));
  }

  server_dispatcher_ = *std::move(dispatcher);

  // Create a dispatcher for Wlansoftmac device as a FIDL client.
  dispatcher = fdf::Dispatcher::Create(
      0, "wlansoftmac_client", [&](fdf_dispatcher_t*) { server_dispatcher_.ShutdownAsync(); });

  if (dispatcher.is_error()) {
    ZX_ASSERT_MSG(false, "Creating client dispatcher error: %s",
                  zx_status_get_string(dispatcher.status_value()));
  }

  client_dispatcher_ = *std::move(dispatcher);

  // Create the client dispatcher for fuchsia_wlan_softmac::WlanSoftmac protocol.
  client_ = fdf::WireSharedClient<fuchsia_wlan_softmac::WlanSoftmac>(std::move(client),
                                                                     client_dispatcher_.get());
}

Device::~Device() { debugfn(); }

// Disable thread safety analysis, as this is a part of device initialization.
// All thread-unsafe work should occur before multiple threads are possible
// (e.g., before MainLoop is started and before DdkAdd() is called), or locks
// should be held.
zx_status_t Device::Bind(fdf::Channel channel) __TA_NO_THREAD_SAFETY_ANALYSIS {
  debugfn();
  infof("Binding our new WLAN device.");

  zx_status_t status;
  status = DdkServiceConnect(fidl::DiscoverableProtocolName<fuchsia_wlan_softmac::WlanSoftmac>,
                             std::move(channel));
  if (status != ZX_OK) {
    errorf("DdkServiceConnect failed: %s", zx_status_get_string(status));
    return status;
  }

  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    errorf("Arena creation failed: %s", arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  auto result = client_.sync().buffer(*std::move(arena))->Query();
  if (!result.ok()) {
    errorf("Failed getting query result (FIDL error %s)", result.status_string());
    return result.status();
  }
  if (result->is_error()) {
    errorf("Failed getting query result (status %s)", zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  if ((status = ConvertWlanSoftmacInfo(result->value()->info, &wlan_softmac_info_)) != ZX_OK) {
    errorf("WlanSoftmacInfo conversion failed (%s)", zx_status_get_string(status));
    return status;
  }

  auto discovery_arena = fdf::Arena::Create(0, 0);
  if (discovery_arena.is_error()) {
    errorf("Arena creation failed: %s", discovery_arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  auto discovery_result =
      client_.sync().buffer(*std::move(discovery_arena))->QueryDiscoverySupport();
  if (!discovery_result.ok()) {
    errorf("Failed getting discovery result (FIDL error %s)", discovery_result.status_string());
    return discovery_result.status();
  }

  ConvertDiscoverySuppport(discovery_result->value()->resp, &discovery_support_);

  auto mac_sublayer_arena = fdf::Arena::Create(0, 0);
  if (mac_sublayer_arena.is_error()) {
    errorf("Arena creation failed: %s", mac_sublayer_arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  auto mac_sublayer_result =
      client_.sync().buffer(*std::move(mac_sublayer_arena))->QueryMacSublayerSupport();
  if (!mac_sublayer_result.ok()) {
    errorf("Failed getting mac sublayer result (FIDL error %s)",
           mac_sublayer_result.status_string());
    return mac_sublayer_result.status();
  }

  if ((status = ConvertMacSublayerSupport(mac_sublayer_result->value()->resp,
                                          &mac_sublayer_support_)) != ZX_OK) {
    errorf("MacSublayerSupport conversion failed (%s)", zx_status_get_string(status));
    return status;
  }

  auto security_arena = fdf::Arena::Create(0, 0);
  if (security_arena.is_error()) {
    errorf("Arena creation failed: %s", security_arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  auto security_result = client_.sync().buffer(*std::move(security_arena))->QuerySecuritySupport();
  if (!security_result.ok()) {
    errorf("Failed getting security result (FIDL error %s)", security_result.status_string());
    return security_result.status();
  }

  ConvertSecuritySupport(security_result->value()->resp, &security_support_);

  auto spectrum_management_arena = fdf::Arena::Create(0, 0);
  if (spectrum_management_arena.is_error()) {
    errorf("Arena creation failed: %s", spectrum_management_arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  auto spectrum_management_result = client_.sync()
                                        .buffer(*std::move(spectrum_management_arena))
                                        ->QuerySpectrumManagementSupport();

  if (!spectrum_management_result.ok()) {
    errorf("Failed getting spectrum management result (FIDL error %s)",
           spectrum_management_result.status_string());
    return spectrum_management_result.status();
  }

  ConvertSpectrumManagementSupport(spectrum_management_result->value()->resp,
                                   &spectrum_management_support_);

  /* End of data type conversion. */

  state_->set_address(common::MacAddr(wlan_softmac_info_.sta_addr));

  switch (wlan_softmac_info_.mac_role) {
    case WLAN_MAC_ROLE_CLIENT:
      infof("Initialize a client MLME.");
      mlme_.reset(new ClientMlme(this, ClientMlmeDefaultConfig()));
      break;
    case WLAN_MAC_ROLE_AP:
      infof("Initialize an AP MLME.");
      mlme_.reset(new ApMlme(this));
      break;
    // TODO(fxbug.dev/44485): Add support for WLAN_MAC_ROLE_MESH.
    default:
      errorf("unsupported MAC role: %u", wlan_softmac_info_.mac_role);
      return ZX_ERR_NOT_SUPPORTED;
  }
  ZX_DEBUG_ASSERT(mlme_ != nullptr);
  status = mlme_->Init();
  if (status != ZX_OK) {
    errorf("could not initialize MLME: %d", status);
    return status;
  }

  status = AddEthDevice();
  if (status != ZX_OK) {
    errorf("could not add eth device: %s", zx_status_get_string(status));
    mlme_->StopMainLoop();
    return status;
  }

  debugf("device added");
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
    errorf("could not get buffer for packet of length %zu", length);
    return nullptr;
  }

  auto packet = std::unique_ptr<Packet>(new Packet(std::move(buffer), length));
  packet->set_peer(peer);
  zx_status_t status = packet->CopyFrom(data, length, 0);
  if (status != ZX_OK) {
    errorf("could not copy to packet: %d", status);
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
  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    errorf("Arena creation failed: %s", arena.status_string());
    return;
  }
  client_dispatcher_.ShutdownAsync();
}

void Device::EthRelease() {
  debugfn();
  // The lifetime of this device is managed by the parent ethernet device, but we don't
  // have a mechanism to make this explicit. EthUnbind is already called at this point,
  // so it's safe to clean up our memory usage.
  DestroySelf();
}

void Device::DdkInit(ddk::InitTxn txn) {}

void Device::DdkUnbind(ddk::UnbindTxn txn) {
  // Saving the input UnbindTxn to the device, ::ddk::UnbindTxn::Reply() will be called with this
  // UnbindTxn in the shutdown callback of the dispatcher, so that we can make sure DdkUnbind()
  // won't end before the dispatcher shutdown.
  unbind_txn_ = std::move(txn);
  client_dispatcher_.ShutdownAsync();
}

void Device::DdkRelease() { delete this; }

zx_status_t Device::EthernetImplQuery(uint32_t options, ethernet_info_t* info) {
  debugfn();
  if (info == nullptr)
    return ZX_ERR_INVALID_ARGS;

  memset(info, 0, sizeof(*info));
  memcpy(info->mac, wlan_softmac_info_.sta_addr, ETH_MAC_SIZE);
  info->features = ETHERNET_FEATURE_WLAN;

  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    errorf("Arena creation failed: %s", arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  auto mac_sublayer_result = client_.sync().buffer(*std::move(arena))->QueryMacSublayerSupport();
  if (!mac_sublayer_result.ok()) {
    errorf("Failed getting mac sublayer result (FIDL error %s)",
           mac_sublayer_result.status_string());
    return mac_sublayer_result.status();
  }

  zx_status_t status = ZX_OK;
  mac_sublayer_support_t mac_sublayer;
  if ((status = ConvertMacSublayerSupport(mac_sublayer_result->value()->resp, &mac_sublayer)) !=
      ZX_OK) {
    errorf("MacSublayerSupport conversion failed (%s)", zx_status_get_string(status));
    return status;
  }

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
    warnf("ethmac not started");
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
    warnf("could not prepare Ethernet packet with len %zu", netbuf->data_size);
    op.Complete(ZX_ERR_NO_RESOURCES);
    return;
  }

  // Forward the packet straight into Rust MLME.
  auto status = mlme_->QueueEthFrameTx(std::move(packet));
  if (status != ZX_OK) {
    warnf("could not queue Ethernet packet err=%s", zx_status_get_string(status));
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
        warnf("WLAN promiscuous not supported yet. see fxbug.dev/29113");
      }
      status = ZX_OK;
      break;
  }

  return status;
}

zx_status_t Device::Start(const rust_wlan_softmac_ifc_protocol_copy_t* ifc,
                          zx::channel* out_sme_channel) {
  debugf("Start");

  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    errorf("Arena creation failed: %s", arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  auto endpoints = fdf::CreateEndpoints<fuchsia_wlan_softmac::WlanSoftmacIfc>();
  if (endpoints.is_error()) {
    errorf("Creating end point error: %s", zx_status_get_string(endpoints.status_value()));
    return endpoints.status_value();
  }

  fdf::BindServer<fdf::WireServer<fuchsia_wlan_softmac::WlanSoftmacIfc>>(
      server_dispatcher_.get(), std::move(endpoints->server), this);

  // The protocol functions are stored in this class, which will act as
  // the server end of WlanSoftmacifc FIDL protocol, and this set of function pointers will be
  // called in the handler functions of FIDL server end.
  wlan_softmac_ifc_protocol_ops_.reset(new wlan_softmac_ifc_protocol_ops_t{
      .status = ifc->ops->status,
      .recv = ifc->ops->recv,
      .complete_tx = ifc->ops->complete_tx,
      .report_tx_status = ifc->ops->report_tx_status,
      .scan_complete = ifc->ops->scan_complete,
  });

  wlan_softmac_ifc_protocol_ = std::make_unique<wlan_softmac_ifc_protocol_t>();
  wlan_softmac_ifc_protocol_->ops = wlan_softmac_ifc_protocol_ops_.get();
  wlan_softmac_ifc_protocol_->ctx = ifc->ctx;

  auto result = client_.sync().buffer(*std::move(arena))->Start(std::move(endpoints->client));
  if (!result.ok()) {
    errorf("change channel failed (FIDL error %s)", result.status_string());
    return result.status();
  }
  if (result->is_error()) {
    errorf("change channel failed (status %d)", result->error_value());
    return result->error_value();
  }
  *out_sme_channel = std::move(result->value()->sme_channel);
  return ZX_OK;
}

zx_status_t Device::DeliverEthernet(cpp20::span<const uint8_t> eth_frame) {
  if (eth_frame.size() > ETH_FRAME_MAX_SIZE) {
    errorf("Attempted to deliver an ethernet frame of invalid length: %zu", eth_frame.size());
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

  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    errorf("Arena creation failed: %s", arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  zx_status_t status = ZX_OK;
  fuchsia_wlan_softmac::wire::WlanTxPacket fidl_tx_packet;
  if ((status = ConvertTxPacket(packet->data(), packet->len(), tx_info, &fidl_tx_packet)) !=
      ZX_OK) {
    errorf("WlanTxPacket conversion failed: %s", zx_status_get_string(status));
    return status;
  }

  auto result = client_.sync().buffer(*arena)->QueueTx(fidl_tx_packet);

  if (!result.ok()) {
    errorf("QueueTx failed (FIDL error %s)", result.status_string());
    return result.status();
  }
  if (result->is_error()) {
    errorf("QueueTx failed (status %s)", zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  // TODO(fxbug.dev/85924): Remove this once we implement WlanSoftmacCompleteTx
  // and allow wlan-softmac drivers to complete transmits asynchronously.
  ZX_DEBUG_ASSERT(!result->value()->enqueue_pending);

  return ZX_OK;
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
  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    errorf("Arena creation failed: %s", arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  char buf[80];
  zx_status_t status = ZX_OK;
  fuchsia_wlan_common::wire::WlanChannel current_channel;
  if ((status = ConvertChannel(state_->channel(), &current_channel)) != ZX_OK) {
    errorf("WlanChannel conversion failed: %s", zx_status_get_string(status));
    return status;
  }

  fuchsia_wlan_common::wire::WlanChannel new_channel;
  if ((status = ConvertChannel(channel, &new_channel)) != ZX_OK) {
    errorf("WlanChannel conversion failed: %s", zx_status_get_string(status));
    return status;
  }

  snprintf(buf, sizeof(buf), "SetChannel: from %s to %s", common::ChanStr(current_channel).c_str(),
           common::ChanStr(new_channel).c_str());

  auto result = client_.sync().buffer(*std::move(arena))->SetChannel(new_channel);
  if (!result.ok()) {
    errorf("%s failed (FIDL error %s)", buf, result.status_string());
    return result.status();
  }
  if (result->is_error()) {
    errorf("%s failed (status %s)", buf, zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  state_->set_channel(channel);

  verbosef("%s succeeded", buf);
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
  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    errorf("Arena creation failed: %s", arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  zx_status_t status = ZX_OK;
  fuchsia_wlan_internal::wire::BssConfig fidl_bss_config;
  if ((status = ConvertBssConfig(*cfg, &fidl_bss_config)) != ZX_OK) {
    errorf("BssConfig conversion failed: %s", zx_status_get_string(status));
    return status;
  }

  auto result = client_.sync().buffer(*std::move(arena))->ConfigureBss(fidl_bss_config);
  if (!result.ok()) {
    errorf("Config bss failed (FIDL error %s)", result.status_string());
    return result.status();
  }
  if (result->is_error()) {
    errorf("Config bss failed (status %s)", zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

// Max size of WlanBcnConfig.
static constexpr size_t kWlanBcnConfigBufferSize =
    fidl::MaxSizeInChannel<fuchsia_wlan_softmac::wire::WlanBcnConfig,
                           fidl::MessageDirection::kSending>();

zx_status_t Device::EnableBeaconing(wlan_bcn_config_t* bcn_cfg) {
  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    errorf("Arena creation failed: %s", arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  if (bcn_cfg != nullptr) {
    ZX_DEBUG_ASSERT(
        ValidateFrame("Malformed beacon template",
                      {reinterpret_cast<const uint8_t*>(bcn_cfg->packet_template.mac_frame_buffer),
                       bcn_cfg->packet_template.mac_frame_size}));
  }
  fidl::Arena<kWlanBcnConfigBufferSize> fidl_arena;
  fuchsia_wlan_softmac::wire::WlanBcnConfig fidl_bcn_cfg;
  ConvertBcn(*bcn_cfg, &fidl_bcn_cfg, fidl_arena);

  auto result = client_.sync().buffer(*std::move(arena))->EnableBeaconing(fidl_bcn_cfg);
  if (!result.ok()) {
    errorf("EnableBeaconing failed (FIDL error %s)", result.status_string());
    return result.status();
  }
  if (result->is_error()) {
    errorf("EnableBeaconing failed (status %s)", zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

zx_status_t Device::ConfigureBeacon(std::unique_ptr<Packet> beacon) {
  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    errorf("Arena creation failed: %s", arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  ZX_DEBUG_ASSERT(beacon.get() != nullptr);
  if (beacon.get() == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  ZX_DEBUG_ASSERT(ValidateFrame("Malformed beacon template", {beacon->data(), beacon->size()}));

  zx_status_t status = ZX_OK;
  wlan_tx_packet_t tx_packet = beacon->AsWlanTxPacket();
  fuchsia_wlan_softmac::wire::WlanTxPacket fidl_tx_packet;
  if ((status = ConvertTxPacket(tx_packet.mac_frame_buffer, tx_packet.mac_frame_size,
                                tx_packet.info, &fidl_tx_packet)) != ZX_OK) {
    errorf("WlanTxPacket conversion failed: %s", zx_status_get_string(status));
    return status;
  }

  auto result = client_.sync().buffer(*std::move(arena))->ConfigureBeacon(fidl_tx_packet);
  if (!result.ok()) {
    errorf("ConfigureBeacon failed (FIDL error %s)", result.status_string());
    return result.status();
  }
  if (result->is_error()) {
    errorf("ConfigureBeacon failed (status %s)", zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

zx_status_t Device::SetKey(wlan_key_config_t* key_config) {
  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    errorf("Arena creation failed: %s", arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  zx_status_t status = ZX_OK;
  fidl::Arena fidl_arena;
  fuchsia_wlan_softmac::wire::WlanKeyConfig fidl_key_config;
  if ((status = ConvertKeyConfig(*key_config, &fidl_key_config, fidl_arena)) != ZX_OK) {
    errorf("WlanKeyConfig conversion failed: %s", zx_status_get_string(status));
    return status;
  }

  auto result = client_.sync().buffer(*std::move(arena))->SetKey(fidl_key_config);
  if (!result.ok()) {
    errorf("SetKey failed (FIDL error %s)", result.status_string());
    return result.status();
  }
  if (result->is_error()) {
    errorf("SetKey failed (status %s)", zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

zx_status_t Device::StartPassiveScan(const wlan_softmac_passive_scan_args_t* passive_scan_args,
                                     uint64_t* out_scan_id) {
  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    errorf("Arena creation failed: %s", arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  fidl::Arena fidl_arena;
  fuchsia_wlan_softmac::wire::WlanSoftmacPassiveScanArgs fidl_passive_scan_args;
  ConvertPassiveScanArgs(*passive_scan_args, &fidl_passive_scan_args, fidl_arena);

  auto result = client_.sync().buffer(*std::move(arena))->StartPassiveScan(fidl_passive_scan_args);
  if (!result.ok()) {
    errorf("StartPassiveScan failed (FIDL error %s)", result.status_string());
    return result.status();
  }
  if (result->is_error()) {
    errorf("StartPassiveScan failed (status %s)", zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  *out_scan_id = result->value()->scan_id;
  return ZX_OK;
}

// Max size of WlanSoftmacActiveScanArgs.
static constexpr size_t kWlanSoftmacActiveScanArgsBufferSize =
    fidl::MaxSizeInChannel<fuchsia_wlan_softmac::wire::WlanSoftmacActiveScanArgs,
                           fidl::MessageDirection::kSending>();

zx_status_t Device::StartActiveScan(const wlan_softmac_active_scan_args_t* active_scan_args,
                                    uint64_t* out_scan_id) {
  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    errorf("Arena creation failed: %s", arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  fidl::Arena<kWlanSoftmacActiveScanArgsBufferSize> fidl_arena;
  fuchsia_wlan_softmac::wire::WlanSoftmacActiveScanArgs fidl_active_scan_args;
  ConvertActiveScanArgs(*active_scan_args, &fidl_active_scan_args, fidl_arena);
  auto result = client_.sync().buffer(*std::move(arena))->StartActiveScan(fidl_active_scan_args);
  if (!result.ok()) {
    errorf("StartActiveScan failed (FIDL error %s)", result.status_string());
    return result.status();
  }
  if (result->is_error()) {
    errorf("StartActiveScan failed (status %s)", zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  *out_scan_id = result->value()->scan_id;
  return ZX_OK;
}

zx_status_t Device::CancelScan(uint64_t scan_id) {
  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    errorf("Arena creation failed: %s", arena.status_string());
    return ZX_ERR_INTERNAL;
  }
  auto result = client_.sync().buffer(*std::move(arena))->CancelScan(scan_id);
  if (!result.ok()) {
    errorf("CancelScan Failed (FIDL error %s)", result.status_string());
  }
  return result.status();
}

zx_status_t Device::ConfigureAssoc(wlan_assoc_ctx_t* assoc_ctx) {
  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    errorf("Arena creation failed: %s", arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  zx_status_t status = ZX_OK;
  fuchsia_hardware_wlan_associnfo::wire::WlanAssocCtx fidl_assoc_ctx;
  if ((status = ConvertAssocCtx(*assoc_ctx, &fidl_assoc_ctx)) != ZX_OK) {
    errorf("WlanAssocCtx conversion failed: %s", zx_status_get_string(status));
    return status;
  }

  auto result = client_.sync().buffer(*std::move(arena))->ConfigureAssoc(fidl_assoc_ctx);
  if (!result.ok()) {
    errorf("ConfigureAssoc failed (FIDL error %s)", result.status_string());
    return result.status();
  }
  if (result->is_error()) {
    errorf("ConfigureAssoc failed (status %s)", zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

zx_status_t Device::ClearAssoc(const uint8_t peer_addr[fuchsia_wlan_ieee80211_MAC_ADDR_LEN]) {
  auto arena = fdf::Arena::Create(0, 0);
  if (arena.is_error()) {
    errorf("Arena creation failed: %s", arena.status_string());
    return ZX_ERR_INTERNAL;
  }

  fidl::Array<uint8_t, fuchsia_wlan_ieee80211::wire::kMacAddrLen> fidl_peer_addr;
  memcpy(fidl_peer_addr.begin(), peer_addr, fuchsia_wlan_ieee80211::wire::kMacAddrLen);
  auto result = client_.sync().buffer(*std::move(arena))->ClearAssoc(fidl_peer_addr);
  if (!result.ok()) {
    errorf("ClearAssoc failed (FIDL error %s)", result.status_string());
    return result.status();
  }
  if (result->is_error()) {
    errorf("ClearAssoc failed (status %s)", zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

void Device::Status(StatusRequestView request, fdf::Arena& arena,
                    StatusCompleter::Sync& completer) {
  // No one is using this function right now, saving ink for the mid product.
  ZX_PANIC("Status is not supportted.");
}

void Device::Recv(RecvRequestView request, fdf::Arena& arena, RecvCompleter::Sync& completer) {
  zx_status_t status = ZX_OK;
  wlan_rx_packet_t rx_packet;

  {
    // Lock the buffer operations to prevent data corruption when multiple thread are calling into
    // this function.
    std::lock_guard lock(rx_lock_);
    if ((status = ConvertRxPacket(request->packet, &rx_packet)) != ZX_OK) {
      errorf("RxPacket conversion failed: %s", zx_status_get_string(status));
    }

    wlan_softmac_ifc_protocol_->ops->recv(wlan_softmac_ifc_protocol_->ctx, &rx_packet);
    if (unlikely(request->packet.mac_frame.count() > PRE_ALLOC_RECV_BUFFER_SIZE)) {
      // Freeing the frame buffer allocated in ConvertRxPacket() above.
      memset(const_cast<uint8_t*>(rx_packet.mac_frame_buffer), 0, rx_packet.mac_frame_size);
      free(const_cast<uint8_t*>(rx_packet.mac_frame_buffer));
    } else {
      memset(pre_alloc_recv_buffer, 0, PRE_ALLOC_RECV_BUFFER_SIZE);
    }
  }

  completer.buffer(arena).Reply();
}
void Device::CompleteTx(CompleteTxRequestView request, fdf::Arena& arena,
                        CompleteTxCompleter::Sync& completer) {
  // No one is using this interface right now, saving ink for the mid product.
  ZX_PANIC("CompleteTx is not supportted.");
}
void Device::ReportTxStatus(ReportTxStatusRequestView request, fdf::Arena& arena,
                            ReportTxStatusCompleter::Sync& completer) {
  zx_status_t status = ZX_OK;
  wlan_tx_status_t tx_status;

  if ((status = ConvertTxStatus(request->tx_status, &tx_status)) != ZX_OK) {
    errorf("TxStatus conversion failed: %s", zx_status_get_string(status));
  }

  wlan_softmac_ifc_protocol_->ops->report_tx_status(wlan_softmac_ifc_protocol_->ctx, &tx_status);

  completer.buffer(arena).Reply();
}
void Device::ScanComplete(ScanCompleteRequestView request, fdf::Arena& arena,
                          ScanCompleteCompleter::Sync& completer) {
  wlan_softmac_ifc_protocol_->ops->scan_complete(wlan_softmac_ifc_protocol_->ctx, request->status,
                                                 request->scan_id);
  completer.buffer(arena).Reply();
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
