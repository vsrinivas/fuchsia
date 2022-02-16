// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wlantap-mac.h"

#include <fuchsia/hardware/wlan/softmac/cpp/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>
#include <fuchsia/wlan/device/cpp/fidl.h>
#include <fuchsia/wlan/ieee80211/c/banjo.h>
#include <fuchsia/wlan/internal/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>

#include <mutex>

#include <wlan/common/channel.h>
#include <wlan/common/phy.h>

#include "utils.h"

namespace wlan {

namespace wlantap = ::fuchsia::wlan::tap;
namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_device = ::fuchsia::wlan::device;

namespace {

// TODO(fxbug.dev/93459) Prune unnecessary fields from phy_config
struct WlantapMacImpl : WlantapMac {
  WlantapMacImpl(zx_device_t* phy_device, uint16_t id, wlan_common::WlanMacRole role,
                 const wlantap::WlantapPhyConfig* phy_config, Listener* listener,
                 zx::channel sme_channel)
      : id_(id),
        role_(role),
        phy_config_(phy_config),
        listener_(listener),
        sme_channel_(std::move(sme_channel)) {}

  static void DdkUnbind(void* ctx) {
    auto& self = *static_cast<WlantapMacImpl*>(ctx);
    self.Unbind();
  }

  static void DdkRelease(void* ctx) { delete static_cast<WlantapMacImpl*>(ctx); }

  // WlanSoftmac protocol impl

  static zx_status_t WlanSoftmacQuery(void* ctx, wlan_softmac_info_t* mac_info) {
    auto& self = *static_cast<WlantapMacImpl*>(ctx);
    ConvertTapPhyConfig(mac_info, *self.phy_config_);
    return ZX_OK;
  }

  static void WlanSoftmacQueryDiscoverySupport(void* ctx, discovery_support_t* support) {
    auto& self = *static_cast<WlantapMacImpl*>(ctx);
    *support = ConvertDiscoverySupport(self.phy_config_->discovery_support);
  }

  static void WlanSoftmacQueryMacSublayerSupport(void* ctx, mac_sublayer_support_t* support) {
    auto& self = *static_cast<WlantapMacImpl*>(ctx);
    *support = ConvertMacSublayerSupport(self.phy_config_->mac_sublayer_support);
  }

  static void WlanSoftmacQuerySecuritySupport(void* ctx, security_support_t* support) {
    auto& self = *static_cast<WlantapMacImpl*>(ctx);
    *support = ConvertSecuritySupport(self.phy_config_->security_support);
  }

  static void WlanSoftmacQuerySpectrumManagementSupport(void* ctx,
                                                        spectrum_management_support_t* support) {
    auto& self = *static_cast<WlantapMacImpl*>(ctx);
    *support = ConvertSpectrumManagementSupport(self.phy_config_->spectrum_management_support);
  }

  static zx_status_t WlanSoftmacStart(void* ctx, const wlan_softmac_ifc_protocol_t* ifc,
                                      zx_handle_t* out_sme_channel) {
    auto& self = *static_cast<WlantapMacImpl*>(ctx);
    {
      std::lock_guard<std::mutex> guard(self.lock_);
      if (self.ifc_.is_valid()) {
        return ZX_ERR_ALREADY_BOUND;
      }
      if (!self.sme_channel_.is_valid()) {
        return ZX_ERR_ALREADY_BOUND;
      }
      self.ifc_ = ddk::WlanSoftmacIfcProtocolClient(ifc);
    }
    self.listener_->WlantapMacStart(self.id_);
    *out_sme_channel = self.sme_channel_.release();
    return ZX_OK;
  }

  static void WlanSoftmacStop(void* ctx) {
    auto& self = *static_cast<WlantapMacImpl*>(ctx);
    {
      std::lock_guard<std::mutex> guard(self.lock_);
      self.ifc_.clear();
    }
    self.listener_->WlantapMacStop(self.id_);
  }

  static zx_status_t WlanSoftmacQueueTx(void* ctx, const wlan_tx_packet_t* packet,
                                        bool* out_enqueue_pending) {
    auto& self = *static_cast<WlantapMacImpl*>(ctx);
    self.listener_->WlantapMacQueueTx(self.id_, packet);
    *out_enqueue_pending = false;
    return ZX_OK;
  }

  static zx_status_t WlanSoftmacSetChannel(void* ctx, const wlan_channel_t* channel) {
    auto& self = *static_cast<WlantapMacImpl*>(ctx);
    if (!wlan::common::IsValidChan(*channel)) {
      return ZX_ERR_INVALID_ARGS;
    }
    self.listener_->WlantapMacSetChannel(self.id_, channel);
    return ZX_OK;
  }

  static zx_status_t WlanSoftmacConfigureBss(void* ctx, const bss_config_t* config) {
    auto& self = *static_cast<WlantapMacImpl*>(ctx);
    bool expected_remote = self.role_ == wlan_common::WlanMacRole::CLIENT;
    if (config->remote != expected_remote) {
      return ZX_ERR_INVALID_ARGS;
    }
    self.listener_->WlantapMacConfigureBss(self.id_, config);
    return ZX_OK;
  }

  static zx_status_t WlanSoftmacEnableBeaconing(void* ctx, const wlan_bcn_config_t* bcn_cfg) {
    // This is the test driver, so we can just pretend beaconing was enabled.
    (void)bcn_cfg;
    return ZX_OK;
  }

  static zx_status_t WlanSoftmacConfigureBeacon(void* ctx, const wlan_tx_packet_t* pkt) {
    // This is the test driver, so we can just pretend the beacon was configured.
    (void)pkt;
    return ZX_OK;
  }

  static zx_status_t WlanSoftmacSetKey(void* ctx, const wlan_key_config_t* key_config) {
    auto& self = *static_cast<WlantapMacImpl*>(ctx);
    self.listener_->WlantapMacSetKey(self.id_, key_config);
    return ZX_OK;
  }

  static zx_status_t WlanSoftmacConfigureAssoc(void* ctx, const wlan_assoc_ctx* assoc_ctx) {
    // This is the test driver, so we can just pretend the association was configured.
    (void)assoc_ctx;
    // TODO(fxbug.dev/28907): Evalute the use and implement
    return ZX_OK;
  }

  static zx_status_t WlanSoftmacClearAssoc(
      void* ctx, const uint8_t peer_addr[fuchsia_wlan_ieee80211_MAC_ADDR_LEN]) {
    if (!peer_addr) {
      return ZX_ERR_INVALID_ARGS;
    }
    // TODO(fxbug.dev/28907): Evalute the use and implement
    return ZX_OK;
  }

  // WlantapMac impl

  virtual void Rx(const std::vector<uint8_t>& data, const wlantap::WlanRxInfo& rx_info) override {
    std::lock_guard<std::mutex> guard(lock_);
    if (ifc_.is_valid()) {
      wlan_rx_info_t converted_info = {.rx_flags = rx_info.rx_flags,
                                       .valid_fields = rx_info.valid_fields,
                                       .phy = common::FromFidl(rx_info.phy),
                                       .data_rate = rx_info.data_rate,
                                       .channel = {.primary = rx_info.channel.primary,
                                                   .cbw = static_cast<uint8_t>(rx_info.channel.cbw),
                                                   .secondary80 = rx_info.channel.secondary80},
                                       .mcs = rx_info.mcs,
                                       .rssi_dbm = rx_info.rssi_dbm,
                                       .snr_dbh = rx_info.snr_dbh};
      wlan_rx_packet_t rx_packet = {
          .mac_frame_buffer = data.data(), .mac_frame_size = data.size(), .info = converted_info};
      ifc_.Recv(&rx_packet);
    }
  }

  virtual void Status(uint32_t status) override {
    std::lock_guard<std::mutex> guard(lock_);
    if (ifc_.is_valid()) {
      ifc_.Status(status);
    }
  }

  virtual void ReportTxStatus(const wlan_common::WlanTxStatus& ts) override {
    std::lock_guard<std::mutex> guard(lock_);
    if (ifc_.is_valid()) {
      wlan_tx_status_t converted_tx_status = ConvertTxStatus(ts);
      ifc_.ReportTxStatus(&converted_tx_status);
    }
  }

  void Unbind() {
    {
      std::lock_guard<std::mutex> guard(lock_);
      ifc_.clear();
    }
    device_unbind_reply(device_);
  }

  virtual void RemoveDevice() override { device_async_remove(device_); }

  zx_device_t* device_ = nullptr;
  uint16_t id_;
  wlan_common::WlanMacRole role_;
  std::mutex lock_;
  ddk::WlanSoftmacIfcProtocolClient ifc_ __TA_GUARDED(lock_);
  const wlantap::WlantapPhyConfig* phy_config_;
  Listener* listener_;
  zx::channel sme_channel_;
};

}  // namespace

zx_status_t CreateWlantapMac(zx_device_t* parent_phy, const wlan_common::WlanMacRole role,
                             const wlantap::WlantapPhyConfig* phy_config, uint16_t id,
                             WlantapMac::Listener* listener, zx::channel sme_channel,
                             WlantapMac** ret) {
  char name[ZX_MAX_NAME_LEN + 1];
  snprintf(name, sizeof(name), "mac%u", id);
  std::unique_ptr<WlantapMacImpl> wlan_softmac(
      new WlantapMacImpl(parent_phy, id, role, phy_config, listener, std::move(sme_channel)));
  static zx_protocol_device_t device_ops = {.version = DEVICE_OPS_VERSION,
                                            .unbind = &WlantapMacImpl::DdkUnbind,
                                            .release = &WlantapMacImpl::DdkRelease};
  static wlan_softmac_protocol_ops_t proto_ops = {
      .query = &WlantapMacImpl::WlanSoftmacQuery,
      .query_discovery_support = &WlantapMacImpl::WlanSoftmacQueryDiscoverySupport,
      .query_mac_sublayer_support = &WlantapMacImpl::WlanSoftmacQueryMacSublayerSupport,
      .query_security_support = &WlantapMacImpl::WlanSoftmacQuerySecuritySupport,
      .query_spectrum_management_support =
          &WlantapMacImpl::WlanSoftmacQuerySpectrumManagementSupport,
      .start = &WlantapMacImpl::WlanSoftmacStart,
      .stop = &WlantapMacImpl::WlanSoftmacStop,
      .queue_tx = &WlantapMacImpl::WlanSoftmacQueueTx,
      .set_channel = &WlantapMacImpl::WlanSoftmacSetChannel,
      .configure_bss = &WlantapMacImpl::WlanSoftmacConfigureBss,
      .enable_beaconing = &WlantapMacImpl::WlanSoftmacEnableBeaconing,
      .configure_beacon = &WlantapMacImpl::WlanSoftmacConfigureBeacon,
      .set_key = &WlantapMacImpl::WlanSoftmacSetKey,
      .configure_assoc = &WlantapMacImpl::WlanSoftmacConfigureAssoc,
      .clear_assoc = &WlantapMacImpl::WlanSoftmacClearAssoc,
  };
  device_add_args_t args = {.version = DEVICE_ADD_ARGS_VERSION,
                            .name = name,
                            .ctx = wlan_softmac.get(),
                            .ops = &device_ops,
                            .proto_id = ZX_PROTOCOL_WLAN_SOFTMAC,
                            .proto_ops = &proto_ops};
  zx_status_t status = device_add(parent_phy, &args, &wlan_softmac->device_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not add device: %d", __func__, status);
    return status;
  }
  // Transfer ownership to devmgr
  *ret = wlan_softmac.release();
  return ZX_OK;
}

}  // namespace wlan
