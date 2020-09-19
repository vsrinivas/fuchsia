// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_TESTS_MOCK_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_TESTS_MOCK_DEVICE_H_

#include <fuchsia/wlan/minstrel/cpp/fidl.h>
#include <lib/timekeeper/test_clock.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <fbl/ref_ptr.h>
#include <gtest/gtest.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mlme.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>

#include "test_timer.h"
#include "test_utils.h"

namespace wlan {

static constexpr uint8_t kClientAddress[] = {0x94, 0x3C, 0x49, 0x49, 0x9F, 0x2D};

namespace {

struct WlanPacket {
  std::unique_ptr<Packet> pkt;
  wlan_channel_bandwidth_t cbw;
  wlan_info_phy_type_t phy;
  uint32_t flags;
};

std::pair<zx::channel, zx::channel> make_channel() {
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  return {std::move(local), std::move(remote)};
}

// Reads a fidl_msg_t from a channel.
struct FidlMessage {
  static std::optional<FidlMessage> ReadFromChannel(zx::channel* endpoint) {
    FidlMessage msg = {};
    auto status =
        endpoint->read(ZX_CHANNEL_READ_MAY_DISCARD, msg.bytes, msg.handles, sizeof(msg.bytes),
                       sizeof(msg.handles), &msg.actual_bytes, &msg.actual_handles);
    if (status != ZX_OK) {
      return {std::nullopt};
    }
    return {std::move(msg)};
  }

  fidl_msg_t get() {
    return {.bytes = bytes,
            .handles = handles,
            .num_bytes = actual_bytes,
            .num_handles = actual_handles};
  }

  fbl::Span<uint8_t> data() { return {bytes, actual_bytes}; }

  FIDL_ALIGNDECL uint8_t bytes[256];
  zx_handle_t handles[256];
  uint32_t actual_bytes;
  uint32_t actual_handles;
};

// TODO(hahnr): Support for failing various device calls.
struct MockDevice : public DeviceInterface {
 public:
  using PacketList = std::vector<WlanPacket>;
  using KeyList = std::vector<wlan_key_config_t>;

  MockDevice(common::MacAddr addr = common::MacAddr(kClientAddress)) : sta_assoc_ctx_{} {
    auto [sme, mlme] = make_channel();
    sme_ = std::move(sme);
    mlme_ = std::move(mlme);

    state = fbl::AdoptRef(new DeviceState);
    state->set_address(addr);

    auto info = &wlanmac_info.ifc_info;
    memcpy(info->mac_addr, addr.byte, 6);
    info->mac_role = WLAN_INFO_MAC_ROLE_CLIENT;
    info->supported_phys = WLAN_INFO_PHY_TYPE_OFDM | WLAN_INFO_PHY_TYPE_HT | WLAN_INFO_PHY_TYPE_VHT;
    info->driver_features = 0;
    info->bands_count = 2;
    info->bands[0] = test_utils::FakeBandInfo(WLAN_INFO_BAND_2GHZ);
    info->bands[1] = test_utils::FakeBandInfo(WLAN_INFO_BAND_5GHZ);
    state->set_channel({
        .primary = 1,
        .cbw = WLAN_CHANNEL_BANDWIDTH__20,
    });
  }

  // DeviceInterface implementation.

  zx_status_t GetTimer(uint64_t id, std::unique_ptr<Timer>* timer) final {
    *timer = CreateTimer(id);
    return ZX_OK;
  }

  std::unique_ptr<Timer> CreateTimer(uint64_t id) {
    return std::make_unique<TestTimer>(id, &clock_);
  }

  zx_handle_t GetSmeChannelRef() final { return mlme_.get(); }

  zx_status_t DeliverEthernet(fbl::Span<const uint8_t> eth_frame) final {
    eth_queue.push_back({eth_frame.cbegin(), eth_frame.cend()});
    return ZX_OK;
  }

  zx_status_t SendWlan(std::unique_ptr<Packet> packet, uint32_t flags) final {
    WlanPacket wlan_packet;
    wlan_packet.pkt = std::move(packet);
    wlan_packet.flags = flags;
    wlan_queue.push_back(std::move(wlan_packet));
    return ZX_OK;
  }

  zx_status_t SendService(fbl::Span<const uint8_t> span) final {
    std::vector<uint8_t> msg(span.cbegin(), span.cend());
    svc_queue.push_back(msg);
    return ZX_OK;
  }

  zx_status_t SetChannel(wlan_channel_t chan) final {
    state->set_channel(chan);
    return ZX_OK;
  }

  zx_status_t SetStatus(uint32_t status) final {
    state->set_online(status == 1);
    return ZX_OK;
  }

  zx_status_t ConfigureBss(wlan_bss_config_t* cfg) final {
    if (!cfg) {
      bss_cfg.reset();
    } else {
      // Copy config which might get freed by the MLME before the result was
      // verified.
      bss_cfg.reset(new wlan_bss_config_t);
      memcpy(bss_cfg.get(), cfg, sizeof(wlan_bss_config_t));
    }
    return ZX_OK;
  }

  zx_status_t ConfigureBeacon(std::unique_ptr<Packet> packet) final {
    beacon = std::move(packet);
    return ZX_OK;
  }

  zx_status_t EnableBeaconing(wlan_bcn_config_t* bcn_cfg) final {
    beaconing_enabled = (bcn_cfg != nullptr);
    return ZX_OK;
  }

  zx_status_t SetKey(wlan_key_config_t* cfg) final {
    keys.push_back(*cfg);
    return ZX_OK;
  }

  zx_status_t StartHwScan(const wlan_hw_scan_config_t* scan_config) override {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t ConfigureAssoc(wlan_assoc_ctx_t* assoc_ctx) final {
    sta_assoc_ctx_ = *assoc_ctx;
    return ZX_OK;
  }
  zx_status_t ClearAssoc(const common::MacAddr& peer_addr) final {
    std::memset(&sta_assoc_ctx_, 0, sizeof(sta_assoc_ctx_));
    return ZX_OK;
  }

  fbl::RefPtr<DeviceState> GetState() final { return state; }

  const wlanmac_info_t& GetWlanInfo() const override final { return wlanmac_info; }

  zx_status_t GetMinstrelPeers(::fuchsia::wlan::minstrel::Peers* peers_fidl) final {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t GetMinstrelStats(const common::MacAddr& addr,
                               ::fuchsia::wlan::minstrel::Peer* resp) final {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Convenience methods.

  void AdvanceTime(zx::duration duration) { clock_.Set(zx::time() + duration); }

  void SetTime(zx::time time) { clock_.Set(time); }

  zx::time GetTime() { return clock_.Now(); }

  wlan_channel_t GetChannel() { return state->channel(); }

  uint16_t GetChannelNumber() { return state->channel().primary; }

  // kNoOrdinal means return the first message as <T> even though it might not
  // be of type T.
  template <typename T>
  std::vector<MlmeMsg<T>> GetServiceMsgs(uint64_t ordinal = MlmeMsg<T>::kNoOrdinal) {
    std::vector<MlmeMsg<T>> ret;
    for (auto iter = svc_queue.begin(); iter != svc_queue.end(); ++iter) {
      auto msg = MlmeMsg<T>::Decode(*iter, ordinal);
      if (msg.has_value()) {
        ret.emplace_back(std::move(msg.value()));
        iter->clear();
      }
    }
    svc_queue.erase(
        std::remove_if(svc_queue.begin(), svc_queue.end(), [](auto& i) { return i.empty(); }),
        svc_queue.end());
    return ret;
  }

  template <typename T>
  std::optional<MlmeMsg<T>> GetNextMsgFromSmeChannel(uint64_t ordinal = MlmeMsg<T>::kNoOrdinal) {
    zx_signals_t observed;
    sme_.wait_one(ZX_CHANNEL_READABLE | ZX_SOCKET_PEER_CLOSED, zx::deadline_after(zx::msec(10)),
                  &observed);
    if (!(observed & ZX_CHANNEL_READABLE)) {
      return {};
    };

    uint32_t read = 0;
    uint8_t buf[ZX_CHANNEL_MAX_MSG_BYTES];

    zx_status_t status = sme_.read(0, buf, nullptr, ZX_CHANNEL_MAX_MSG_BYTES, 0, &read, nullptr);
    ZX_ASSERT(status == ZX_OK);

    return MlmeMsg<T>::Decode(fbl::Span{buf, read}, ordinal);
  }

  template <typename T>
  MlmeMsg<T> AssertNextMsgFromSmeChannel(uint64_t ordinal = MlmeMsg<T>::kNoOrdinal) {
    zx_signals_t observed;
    sme_.wait_one(ZX_CHANNEL_READABLE | ZX_SOCKET_PEER_CLOSED, zx::time::infinite(), &observed);
    ZX_ASSERT(observed & ZX_CHANNEL_READABLE);

    uint32_t read = 0;
    uint8_t buf[ZX_CHANNEL_MAX_MSG_BYTES];

    zx_status_t status = sme_.read(0, buf, nullptr, ZX_CHANNEL_MAX_MSG_BYTES, 0, &read, nullptr);
    ZX_ASSERT(status == ZX_OK);

    auto msg = MlmeMsg<T>::Decode(fbl::Span{buf, read}, ordinal);
    ZX_ASSERT(msg.has_value());
    return std::move(msg).value();
  }

  std::vector<std::vector<uint8_t>> GetEthPackets() {
    std::vector<std::vector<uint8_t>> tmp;
    tmp.swap(eth_queue);
    return tmp;
  }

  PacketList GetWlanPackets() { return std::move(wlan_queue); }

  KeyList GetKeys() { return keys; }

  const wlan_assoc_ctx_t* GetStationAssocContext(void) { return &sta_assoc_ctx_; }

  bool AreQueuesEmpty() { return wlan_queue.empty() && svc_queue.empty() && eth_queue.empty(); }

  std::optional<FidlMessage> NextTxMlmeMsg() { return FidlMessage::ReadFromChannel(&sme_); }

  fbl::RefPtr<DeviceState> state;
  wlanmac_info_t wlanmac_info;
  PacketList wlan_queue;
  std::vector<std::vector<uint8_t>> svc_queue;
  std::vector<std::vector<uint8_t>> eth_queue;
  std::unique_ptr<wlan_bss_config_t> bss_cfg;
  KeyList keys;
  std::unique_ptr<Packet> beacon;
  bool beaconing_enabled;
  wlan_assoc_ctx_t sta_assoc_ctx_;
  zx::channel sme_;
  zx::channel mlme_;

 private:
  timekeeper::TestClock clock_;
};

}  // namespace
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_TESTS_MOCK_DEVICE_H_
