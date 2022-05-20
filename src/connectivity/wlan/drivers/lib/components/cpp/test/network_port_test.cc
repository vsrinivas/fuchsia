// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <wlan/drivers/components/network_port.h>
#include <zxtest/zxtest.h>

#include "test_network_device_ifc.h"

namespace {

using wlan::drivers::components::NetworkPort;
using wlan::drivers::components::test::TestNetworkDeviceIfc;

constexpr uint32_t kTestMtu = 1514;

class TestNetworkPortInterface : public NetworkPort::Callbacks {
 public:
  void SetMtu(uint32_t mtu) { mtu_ = mtu; }
  // NetworkPort::Callbacks implementation
  uint32_t PortGetMtu() override { return mtu_; }
  void PortGetStatus(port_status_t* out_status) override { port_get_status_.Call(out_status); }
  void PortRemoved() override { removed_.Call(); }
  void MacGetAddress(uint8_t out_mac[6]) override { mac_get_address_.Call(out_mac); }
  void MacGetFeatures(features_t* out_features) override { mac_get_features_.Call(out_features); }
  void MacSetMode(mode_t mode, cpp20::span<const uint8_t> multicast_macs) override {
    mac_set_mode_.Call(mode, multicast_macs);
  }
  mock_function::MockFunction<void> removed_;
  mock_function::MockFunction<void, port_status_t*> port_get_status_;
  mock_function::MockFunction<void, uint8_t*> mac_get_address_;
  mock_function::MockFunction<void, features_t*> mac_get_features_;
  mock_function::MockFunction<void, mode_t, cpp20::span<const uint8_t>> mac_set_mode_;

  uint32_t mtu_ = kTestMtu;
};

TEST(NetworkPortTest, Init) {
  TestNetworkPortInterface port_ifc;
  {
    TestNetworkDeviceIfc netdev_ifc;
    constexpr uint8_t kPortId = 13;

    NetworkPort port_(netdev_ifc.GetProto(), port_ifc, kPortId);

    netdev_ifc.add_port_.ExpectCallWithMatcher(
        [&](uint8_t port_id, const network_port_protocol_t* proto) {
          EXPECT_EQ(port_id, kPortId);
          EXPECT_EQ(proto->ctx, &port_);
          EXPECT_NOT_NULL(proto->ops);
        });

    port_.Init(NetworkPort::Role::Client);
    netdev_ifc.add_port_.VerifyAndClear();
    port_ifc.removed_.ExpectCall();
  }
  port_ifc.removed_.VerifyAndClear();
}

TEST(NetworkPortTest, RemovePort) {
  TestNetworkPortInterface port_ifc;
  TestNetworkDeviceIfc netdev_ifc;
  constexpr uint8_t kPortId = 13;

  NetworkPort port_(netdev_ifc.GetProto(), port_ifc, kPortId);

  netdev_ifc.remove_port_.ExpectCall(kPortId);
  port_ifc.removed_.ExpectCall();

  port_.Init(NetworkPort::Role::Client);
  port_.RemovePort();

  netdev_ifc.remove_port_.VerifyAndClear();
  port_ifc.removed_.VerifyAndClear();
}

TEST(NetworkPortTest, Destructor) {
  TestNetworkPortInterface port_ifc;
  TestNetworkDeviceIfc netdev_ifc;
  constexpr uint8_t kPortId = 13;

  auto port = std::make_unique<NetworkPort>(netdev_ifc.GetProto(), port_ifc, kPortId);

  netdev_ifc.add_port_.ExpectCallWithMatcher([&](uint8_t, const network_port_protocol_t*) {});

  port->Init(NetworkPort::Role::Client);
  netdev_ifc.add_port_.VerifyAndClear();

  // When the port is destroyed it should call remove port which should call removed.
  netdev_ifc.remove_port_.ExpectCall(kPortId);
  port_ifc.removed_.ExpectCall();
  port.reset();
  netdev_ifc.remove_port_.VerifyAndClear();
  port_ifc.removed_.VerifyAndClear();
}

TEST(NetworkPortTest, PortWithEmptyProto) {
  constexpr network_device_ifc_protocol_t kEmptyProto = {};
  TestNetworkPortInterface port_ifc;
  constexpr uint8_t kPortId = 0;
  // Ensure that this object can be constructed and destructed even with an empty proto
  NetworkPort port(kEmptyProto, port_ifc, kPortId);
}

TEST(NetworkPortTest, GetInfoClient) {
  TestNetworkPortInterface port_ifc;
  {
    TestNetworkDeviceIfc netdev_ifc;
    constexpr uint8_t kPortId = 7;

    NetworkPort port(netdev_ifc.GetProto(), port_ifc, kPortId);

    port.Init(NetworkPort::Role::Client);

    port_info_t info;
    port.NetworkPortGetInfo(&info);

    EXPECT_EQ(info.port_class,
              static_cast<uint8_t>(fuchsia_hardware_network::wire::DeviceClass::kWlan));
    port_ifc.removed_.ExpectCall();
  }
  port_ifc.removed_.VerifyAndClear();
}

TEST(NetworkPortTest, GetInfoAp) {
  TestNetworkPortInterface port_ifc;
  {
    TestNetworkDeviceIfc netdev_ifc;
    constexpr uint8_t kPortId = 7;

    NetworkPort port(netdev_ifc.GetProto(), port_ifc, kPortId);

    port.Init(NetworkPort::Role::Ap);

    port_info_t info;
    port.NetworkPortGetInfo(&info);

    EXPECT_EQ(info.port_class,
              static_cast<uint8_t>(fuchsia_hardware_network::wire::DeviceClass::kWlanAp));
    port_ifc.removed_.ExpectCall();
  }
  port_ifc.removed_.VerifyAndClear();
}

struct NetworkPortTestFixture : public ::zxtest::Test {
  static constexpr uint8_t kPortId = 13;

  NetworkPortTestFixture()
      : port_(std::make_unique<NetworkPort>(netdev_ifc_.GetProto(), port_ifc_, kPortId)) {
    port_->Init(NetworkPort::Role::Client);
  }

  void TearDown() override {
    // Expect the call to 'removed' as part of the destruction of NetworkPort, destroy it manually
    // so that afterwards we can verify that the 'removed' call happened.
    port_ifc_.removed_.ExpectCall();
    port_.reset();
    port_ifc_.removed_.VerifyAndClear();
  }

  TestNetworkPortInterface port_ifc_;
  TestNetworkDeviceIfc netdev_ifc_;
  std::unique_ptr<NetworkPort> port_;
};

TEST_F(NetworkPortTestFixture, PortId) { EXPECT_EQ(port_->PortId(), kPortId); }

TEST_F(NetworkPortTestFixture, GetInfo) {
  constexpr uint8_t kEthFrame =
      static_cast<uint8_t>(fuchsia_hardware_network::wire::FrameType::kEthernet);
  constexpr uint32_t kRawFrameFeature = fuchsia_hardware_network::wire::kFrameFeaturesRaw;

  port_info_t info;
  port_->NetworkPortGetInfo(&info);
  // Should be a WLAN port in the default setting
  EXPECT_EQ(info.port_class,
            static_cast<uint8_t>(fuchsia_hardware_network::wire::DeviceClass::kWlan));

  // Must support at least reception of ethernet frames
  cpp20::span<const uint8_t> rx_types(info.rx_types_list, info.rx_types_count);
  EXPECT_NE(std::find(rx_types.begin(), rx_types.end(), kEthFrame), rx_types.end());

  // Must support at least transmission of raw ethernet frames
  cpp20::span<const tx_support_t> tx_types(info.tx_types_list, info.tx_types_count);
  auto is_raw_ethernet = [&](const tx_support_t& support) {
    return support.features == kRawFrameFeature && support.type == kEthFrame;
  };
  EXPECT_NE(std::find_if(tx_types.begin(), tx_types.end(), is_raw_ethernet), tx_types.end());
}

TEST_F(NetworkPortTestFixture, GetPortStatus) {
  ASSERT_FALSE(port_->IsOnline());

  port_status_t status;
  port_ifc_.port_get_status_.ExpectCall(&status);
  port_->NetworkPortGetStatus(&status);
  // After construction status should be offline, which means flags are zero.
  EXPECT_EQ(status.flags, 0u);
  EXPECT_EQ(status.mtu, kTestMtu);
  // Testing of online status in next test, this just verifies correct propagation of calls.
}

TEST_F(NetworkPortTestFixture, PortStatus) {
  constexpr uint32_t kOnline =
      static_cast<uint32_t>(fuchsia_hardware_network::wire::StatusFlags::kOnline);

  ASSERT_FALSE(port_->IsOnline());
  // When the port goes online it should call PortStatusChanged and the flags field should now
  // indicate that the port is online.
  const port_status_t* changed_status = nullptr;
  const port_status_t* get_status = nullptr;
  netdev_ifc_.port_status_changed_.ExpectCallWithMatcher(
      [&](uint8_t port_id, const port_status_t* status) {
        EXPECT_EQ(port_id, kPortId);
        EXPECT_EQ(status->mtu, kTestMtu);
        EXPECT_EQ(status->flags, kOnline);
        changed_status = status;
      });
  port_ifc_.port_get_status_.ExpectCallWithMatcher([&](port_status_t* status) {
    EXPECT_EQ(status->flags, kOnline);
    get_status = status;
  });
  port_->SetPortOnline(true);
  // Ensure that the interface implementation gets to modify the same status if it wants to.
  EXPECT_NOT_NULL(changed_status);
  EXPECT_NOT_NULL(get_status);
  EXPECT_EQ(changed_status, get_status);
  EXPECT_TRUE(port_->IsOnline());

  // Setting the port status to online again should NOT have any effect or call anything.
  netdev_ifc_.port_status_changed_.ExpectNoCall();
  port_ifc_.port_get_status_.ExpectNoCall();
  port_->SetPortOnline(true);
  EXPECT_TRUE(port_->IsOnline());

  // Setting the port to offline should clear the flags field.
  netdev_ifc_.port_status_changed_.ExpectCallWithMatcher(
      [&](uint8_t port_id, const port_status_t* status) {
        EXPECT_EQ(port_id, kPortId);
        EXPECT_EQ(status->flags, 0u);
      });
  port_ifc_.port_get_status_.ExpectCallWithMatcher(
      [](port_status_t* status) { EXPECT_EQ(status->flags, 0u); });
  port_->SetPortOnline(false);
  EXPECT_FALSE(port_->IsOnline());

  netdev_ifc_.port_status_changed_.VerifyAndClear();
  port_ifc_.port_get_status_.VerifyAndClear();
}

TEST_F(NetworkPortTestFixture, MacGetProto) {
  mac_addr_protocol_t mac_ifc;
  port_->NetworkPortGetMac(&mac_ifc);
  EXPECT_EQ(mac_ifc.ctx, port_.get());
  ASSERT_NOT_NULL(mac_ifc.ops);
}

struct NetworkPortMacTestFixture : public NetworkPortTestFixture {
  NetworkPortMacTestFixture() : mac_ifc_(GetMacProto()), mac_(&mac_ifc_) {}

  mac_addr_protocol_t GetMacProto() {
    mac_addr_protocol_t mac_proto;
    port_->NetworkPortGetMac(&mac_proto);
    return mac_proto;
  }

  mac_addr_protocol_t mac_ifc_;
  ::ddk::MacAddrProtocolClient mac_;
};

TEST_F(NetworkPortMacTestFixture, MacGetAddress) {
  constexpr uint8_t kMacAddr[6] = {0x0C, 0x00, 0x0F, 0xF0, 0x0E, 0xE0};
  port_ifc_.mac_get_address_.ExpectCallWithMatcher(
      [&](uint8_t* out_mac) { memcpy(out_mac, kMacAddr, sizeof(kMacAddr)); });

  uint8_t mac_addr[6];
  mac_.GetAddress(mac_addr);
  EXPECT_BYTES_EQ(mac_addr, kMacAddr, sizeof(kMacAddr));
}

TEST_F(NetworkPortMacTestFixture, MacGetFeatures) {
  constexpr uint32_t kSupportedModes = MODE_PROMISCUOUS | MODE_MULTICAST_FILTER;
  constexpr uint32_t kNumMulticastFilters = 42;
  port_ifc_.mac_get_features_.ExpectCallWithMatcher([&](features_t* out_features) {
    out_features->supported_modes = kSupportedModes;
    out_features->multicast_filter_count = kNumMulticastFilters;
  });

  features_t features;
  mac_.GetFeatures(&features);
  EXPECT_EQ(features.supported_modes, kSupportedModes);
  EXPECT_EQ(features.multicast_filter_count, kNumMulticastFilters);
  port_ifc_.mac_get_features_.VerifyAndClear();
}

TEST_F(NetworkPortMacTestFixture, MacSetMode) {
  constexpr std::array<uint8_t, 12> kMulticastMacs = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                                      0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A};
  constexpr mode_t kMode = MODE_MULTICAST_FILTER;
  port_ifc_.mac_set_mode_.ExpectCallWithMatcher([&](mode_t mode,
                                                    cpp20::span<const uint8_t> multicast_macs) {
    EXPECT_EQ(mode, kMode);
    ASSERT_EQ(multicast_macs.size(), kMulticastMacs.size());
    EXPECT_TRUE(std::equal(kMulticastMacs.begin(), kMulticastMacs.end(), multicast_macs.begin()));
  });

  mac_.SetMode(kMode, kMulticastMacs.data(), kMulticastMacs.size());
  port_ifc_.mac_set_mode_.VerifyAndClear();
}

}  // namespace
