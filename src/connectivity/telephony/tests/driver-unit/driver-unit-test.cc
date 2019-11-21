// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/telephony/transport/llcpp/fidl.h>
#include <fuchsia/hardware/usb/peripheral/llcpp/fidl.h>
#include <fuchsia/hardware/usb/virtual/bus/llcpp/fidl.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/port.h>
#include <unistd.h>
#include <zircon/hw/usb.h>

#include <array>
#include <endian.h>

#include <fbl/string.h>
#include <gtest/gtest.h>

#include <src/connectivity/telephony/drivers/qmi-usb-transport/qmi-usb-transport.h>
#include <src/lib/isolated_devmgr/usb-virtual-bus.h>

namespace {
constexpr std::array<uint8_t, qmi_usb::kMacAddrLen> kEthClientMacAddr = {10, 11, 12, 13, 14, 15};
constexpr std::array<uint8_t, qmi_usb::kMacAddrLen> kEthBroadcastMacAddr = {0xff, 0xff, 0xff,
                                                                            0xff, 0xff, 0xff};
constexpr std::array<uint8_t, qmi_usb::kIpv4AddrLen> kEthClientIpAddr = {10, 0, 0, 2};
constexpr std::array<uint8_t, qmi_usb::kIpv4AddrLen> kEthHostIpAddr = {10, 0, 0, 3};
constexpr uint32_t kEthMtu = 1518;
class TelDriverUnitTest : public ::testing::Test {
 public:
  void SetEthData(const uint8_t* data, uint32_t data_len) {
    if (data_len <= kEthMtu) {
      data_.assign(data, data + data_len);
    };
  }

  std::vector<uint8_t>& GetEthData() { return data_; }
  uint32_t GetEthDataLen() { return data_.size(); }

 private:
  std::vector<uint8_t> data_;
};

static ethernet_ifc_protocol_ops_t ifc_ops = {
    .recv = [](void* ctx, const void* data_buffer, size_t data_size, uint32_t flags) {
      static_cast<TelDriverUnitTest*>(ctx)->SetEthData(static_cast<const uint8_t*>(data_buffer), data_size);
    },
};

TEST_F(TelDriverUnitTest, EthArpHandling) {
  std::array<uint8_t, 100> tmp;
  zx_device_t* device_parent_ptr = reinterpret_cast<zx_device_t*>(tmp.data());
  auto device = std::make_unique<::qmi_usb::Device>(device_parent_ptr);
  ethernet_ifc_protocol_t ifc = {
      .ops = &ifc_ops,
      .ctx = this,
  };
  device->EthClientInit(&ifc);
  device->EthTxListNodeInit();
  device->EthMtxInit();
  qmi_usb::EthArpFrame eth_arp_frame;
  std::copy(kEthClientMacAddr.begin(), kEthClientMacAddr.end(), eth_arp_frame.eth_hdr.src_mac_addr);
  std::copy(kEthBroadcastMacAddr.begin(), kEthBroadcastMacAddr.end(), eth_arp_frame.eth_hdr.dst_mac_addr);
  eth_arp_frame.eth_hdr.ethertype = betoh16(qmi_usb::kEthertypeArp);
  std::copy(kEthClientMacAddr.begin(), kEthClientMacAddr.end(), eth_arp_frame.arp.src_mac_addr);
  std::copy(kEthBroadcastMacAddr.begin(), kEthBroadcastMacAddr.end(), eth_arp_frame.arp.dst_mac_addr);
  std::copy(kEthClientIpAddr.begin(), kEthClientIpAddr.end(), eth_arp_frame.arp.src_ip_addr);
  std::copy(kEthHostIpAddr.begin(), kEthHostIpAddr.end(), eth_arp_frame.arp.dst_ip_addr);
  std::copy(qmi_usb::kArpReqHdr.begin(), qmi_usb::kArpReqHdr.end(), reinterpret_cast<uint8_t*>(&eth_arp_frame.arp.arp_hdr));
  ethernet_netbuf_t netbuf;
  netbuf.data_buffer = &eth_arp_frame;
  netbuf.data_size = sizeof(eth_arp_frame);

  device->EthernetImplQueueTx(0, &netbuf, NULL, NULL);

  ASSERT_EQ(GetEthDataLen(), static_cast<uint32_t>(qmi_usb::kEthFrameHdrSize + qmi_usb::kArpSize));
  qmi_usb::EthArpFrame eth_arp_resp =
      *reinterpret_cast<qmi_usb::EthArpFrame*>(GetEthData().data());
  ASSERT_EQ(
      memcmp(eth_arp_resp.eth_hdr.dst_mac_addr, kEthClientMacAddr.data(), kEthClientMacAddr.size()),
      0);
  ASSERT_EQ(memcmp(eth_arp_resp.eth_hdr.src_mac_addr, qmi_usb::kFakeMacAddr.data(),
                   qmi_usb::kFakeMacAddr.size()),
            0);
  ASSERT_EQ(betoh16(eth_arp_frame.eth_hdr.ethertype), qmi_usb::kEthertypeArp);
  ASSERT_EQ(
      memcmp(eth_arp_resp.arp.dst_mac_addr, kEthClientMacAddr.data(), kEthClientMacAddr.size()), 0);
  ASSERT_EQ(memcmp(eth_arp_resp.arp.src_mac_addr, qmi_usb::kFakeMacAddr.data(),
                   qmi_usb::kFakeMacAddr.size()),
            0);
  ASSERT_EQ(memcmp(eth_arp_resp.arp.dst_ip_addr, kEthClientIpAddr.data(), kEthClientIpAddr.size()),
            0);
  ASSERT_EQ(memcmp(eth_arp_resp.arp.src_ip_addr, kEthHostIpAddr.data(), kEthHostIpAddr.size()), 0);
  ASSERT_EQ(
      memcmp(&eth_arp_resp.arp.arp_hdr, qmi_usb::kArpRespHdr.data(), qmi_usb::kArpReqHdr.size()),
      0);
}

}  // namespace
