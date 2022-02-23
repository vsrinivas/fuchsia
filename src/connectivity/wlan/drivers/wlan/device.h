// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_WLAN_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_WLAN_DEVICE_H_

#include <fuchsia/hardware/ethernet/cpp/banjo.h>
#include <fuchsia/hardware/wlan/softmac/cpp/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/internal/c/banjo.h>
#include <lib/ddk/driver.h>
#include <lib/zx/channel.h>
#include <zircon/compiler.h>

#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <unordered_set>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fbl/slab_allocator.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mlme.h>
#include <wlan/mlme/packet.h>

namespace wlan {

class Device : public DeviceInterface {
 public:
  Device(zx_device_t* device, wlan_softmac_protocol_t wlan_softmac_proto);
  ~Device();

  zx_status_t Bind();

  // ddk device methods
  void EthUnbind();
  void EthRelease();

  // ddk ethernet_impl_protocol_ops methods
  zx_status_t EthernetImplQuery(uint32_t options, ethernet_info_t* info);
  zx_status_t EthernetImplStart(const ethernet_ifc_protocol_t* ifc)
      __TA_EXCLUDES(ethernet_proxy_lock_);
  void EthernetImplStop() __TA_EXCLUDES(ethernet_proxy_lock_);
  void EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                           ethernet_impl_queue_tx_callback completion_cb, void* cookie);
  zx_status_t EthernetImplSetParam(uint32_t param, int32_t value, const void* data,
                                   size_t data_size);

  // DeviceInterface methods
  zx_status_t Start(const rust_wlan_softmac_ifc_protocol_copy_t* ifc,
                    zx::channel* out_sme_channel) final;
  zx_status_t DeliverEthernet(cpp20::span<const uint8_t> eth_frame) final
      __TA_EXCLUDES(ethernet_proxy_lock_);
  zx_status_t QueueTx(std::unique_ptr<Packet> packet, wlan_tx_info_t tx_info) final;
  zx_status_t SetChannel(wlan_channel_t channel) final;
  zx_status_t SetStatus(uint32_t status) final __TA_EXCLUDES(ethernet_proxy_lock_);
  zx_status_t ConfigureBss(bss_config_t* cfg) final;
  zx_status_t EnableBeaconing(wlan_bcn_config_t* bcn_cfg) final;
  zx_status_t ConfigureBeacon(std::unique_ptr<Packet> beacon) final;
  zx_status_t SetKey(wlan_key_config_t* key_config) final;
  zx_status_t ConfigureAssoc(wlan_assoc_ctx_t* assoc_ctx) final;
  zx_status_t ClearAssoc(const uint8_t[fuchsia_wlan_ieee80211_MAC_ADDR_LEN]) final;
  zx_status_t StartPassiveScan(const wlan_softmac_passive_scan_args_t* passive_scan_args,
                               uint64_t* out_scan_id) final;
  zx_status_t StartActiveScan(const wlan_softmac_active_scan_args_t* active_scan_args,
                              uint64_t* out_scan_id) final;
  fbl::RefPtr<DeviceState> GetState() final;
  const wlan_softmac_info_t& GetWlanSoftmacInfo() const final;
  const discovery_support_t& GetDiscoverySupport() const final;
  const mac_sublayer_support_t& GetMacSublayerSupport() const final;
  const security_support_t& GetSecuritySupport() const final;
  const spectrum_management_support_t& GetSpectrumManagementSupport() const final;

 private:
  enum class DevicePacket : uint64_t {
    kShutdown,
    kPacketQueued,
    kIndication,
    kHwScanComplete,
  };

  zx_status_t AddEthDevice();

  std::unique_ptr<Packet> PreparePacket(const void* data, size_t length, Packet::Peer peer);
  template <typename T>
  std::unique_ptr<Packet> PreparePacket(const void* data, size_t length, Packet::Peer peer,
                                        const T& ctrl_data) {
    auto packet = PreparePacket(data, length, peer);
    if (packet != nullptr) {
      packet->CopyCtrlFrom(ctrl_data);
    }
    return packet;
  }

  // Waits the main loop to finish and frees itself afterwards.
  void DestroySelf();
  // Informs the message loop to shut down. Calling this function more than once
  // has no effect.
  void ShutdownMainLoop();

  zx_device_t* parent_ = nullptr;
  zx_device_t* ethdev_ = nullptr;

  ddk::WlanSoftmacProtocolClient wlan_softmac_proxy_;

  std::mutex ethernet_proxy_lock_;
  ddk::EthernetIfcProtocolClient ethernet_proxy_ __TA_GUARDED(ethernet_proxy_lock_);
  bool mlme_main_loop_dead_ = false;

  // Manages the lifetime of the protocol struct we pass down to the vendor driver. Actual
  // calls to this protocol should only be performed by the vendor driver.
  std::unique_ptr<wlan_softmac_ifc_protocol_ops_t> wlan_softmac_ifc_protocol_ops_;

  wlan_softmac_info_t wlan_softmac_info_ = {};
  discovery_support_t discovery_support_ = {};
  mac_sublayer_support_t mac_sublayer_support_ = {};
  security_support_t security_support_ = {};
  spectrum_management_support_t spectrum_management_support_ = {};
  fbl::RefPtr<DeviceState> state_;

  std::unique_ptr<Mlme> mlme_;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_WLAN_DEVICE_H_
