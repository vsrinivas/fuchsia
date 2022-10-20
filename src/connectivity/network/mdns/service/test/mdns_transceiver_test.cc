// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/transport/mdns_transceiver.h"

#include <fuchsia/hardware/network/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>

#include "src/connectivity/network/mdns/service/common/mdns_addresses.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/lib/testing/predicates/status.h"

namespace mdns::test {

constexpr std::array<uint8_t, 4> kIPv4Address1 = {1, 2, 3, 1};
constexpr std::array<uint8_t, 4> kIPv4Address2 = {4, 5, 6, 1};
constexpr std::array<uint8_t, 16> kIPv6Address1 = {0xfe, 0x80, 0, 0, 0, 0, 0, 0,
                                                   0,    0,    0, 0, 1, 2, 3, 4};
constexpr std::array<uint8_t, 16> kIPv6Address2 = {0xfe, 0x80, 0, 0, 0, 0, 0, 0,
                                                   0,    0,    0, 0, 4, 3, 2, 1};
constexpr std::array<uint8_t, 16> kIPv6AddressNotLinkLocal = {0xff, 0x80, 0, 0, 0, 0, 0, 0,
                                                              0,    0,    0, 0, 4, 3, 2, 1};
constexpr uint8_t kIPv4PrefixLength = 24;
constexpr uint8_t kIPv6PrefixLength = 64;
constexpr uint8_t kID = 1;
constexpr const char kName[] = "test01";

namespace {

fuchsia::net::Ipv4Address ToFIDL(const std::array<uint8_t, 4>& bytes) {
  fuchsia::net::Ipv4Address addr;
  std::copy(bytes.cbegin(), bytes.cend(), addr.addr.begin());
  return addr;
}

fuchsia::net::Ipv6Address ToFIDL(const std::array<uint8_t, 16>& bytes) {
  fuchsia::net::Ipv6Address addr;
  std::copy(bytes.cbegin(), bytes.cend(), addr.addr.begin());
  return addr;
}

void InitAddress(fuchsia::net::interfaces::Address& addr, const std::array<uint8_t, 4>& bytes) {
  fuchsia::net::Subnet subnet{
      .prefix_len = kIPv4PrefixLength,
  };
  subnet.addr.set_ipv4(ToFIDL(bytes));
  addr.set_addr(std::move(subnet));
}

void InitAddress(fuchsia::net::interfaces::Address& addr, const std::array<uint8_t, 16>& bytes) {
  fuchsia::net::Subnet subnet{
      .prefix_len = kIPv6PrefixLength,
  };
  subnet.addr.set_ipv6(ToFIDL(bytes));
  addr.set_addr(std::move(subnet));
}

std::vector<fuchsia::net::interfaces::Address> Addresses1() {
  std::vector<fuchsia::net::interfaces::Address> addresses;
  addresses.reserve(2);
  InitAddress(addresses.emplace_back(), kIPv4Address1);
  InitAddress(addresses.emplace_back(), kIPv6Address1);
  // To verify that non-link-local addresses do not cause a transceiver to be created.
  InitAddress(addresses.emplace_back(), kIPv6AddressNotLinkLocal);
  return addresses;
}

std::vector<fuchsia::net::interfaces::Address> Addresses2() {
  std::vector<fuchsia::net::interfaces::Address> addresses;
  addresses.reserve(2);
  InitAddress(addresses.emplace_back(), kIPv4Address2);
  InitAddress(addresses.emplace_back(), kIPv6Address2);
  return addresses;
}

}  // namespace

class InterfacesWatcherImpl : public fuchsia::net::interfaces::testing::Watcher_TestBase {
 public:
  void NotImplemented_(const std::string& name) override {
    std::cout << "Not implemented: " << name << std::endl;
  }

  void Watch(WatchCallback callback) override {
    ASSERT_FALSE(watch_cb_.has_value());
    watch_cb_ = std::move(callback);
  }

  bool CompleteWatchCallback(fuchsia::net::interfaces::Event event) {
    if (watch_cb_.has_value()) {
      (*watch_cb_)(std::move(event));
      watch_cb_.reset();
      return true;
    } else {
      return false;
    }
  }

 private:
  std::optional<WatchCallback> watch_cb_;
};

class StubInterfaceTransceiver : public MdnsInterfaceTransceiver {
 public:
  StubInterfaceTransceiver(inet::IpAddress address, const std::string& name, uint32_t id,
                           Media media)
      : MdnsInterfaceTransceiver(address, name, id, media),
        ip_versions_(address.is_v4() ? IpVersions::kV4 : IpVersions::kV6) {}

  static std::unique_ptr<MdnsInterfaceTransceiver> Create(inet::IpAddress address,
                                                          const std::string& name, uint32_t id,
                                                          Media media) {
    return std::make_unique<StubInterfaceTransceiver>(address, name, id, media);
  }

 protected:
  enum IpVersions IpVersions() override { return ip_versions_; }
  int SetOptionDisableMulticastLoop() override { return 0; }
  int SetOptionJoinMulticastGroup() override { return 0; }
  int SetOptionOutboundInterface() override { return 0; }
  int SetOptionUnicastTtl() override { return 0; }
  int SetOptionMulticastTtl() override { return 0; }
  int SetOptionFamilySpecific() override { return 0; }
  int Bind() override { return 0; }
  ssize_t SendTo(const void* buffer, size_t size, const inet::SocketAddress& address) override {
    return 0;
  }

  bool Start(InboundMessageCallback callback) override { return true; }
  void Stop() override {}

 private:
  enum IpVersions ip_versions_;
};

class MdnsTransceiverTests : public gtest::TestLoopFixture {
 public:
  MdnsTransceiverTests()
      : binding_(std::make_unique<fidl::Binding<fuchsia::net::interfaces::Watcher>>(
            &fake_watcher_impl_)),
        v4_address1_(inet::IpAddress(ToFIDL(kIPv4Address1))),
        v4_address2_(inet::IpAddress(ToFIDL(kIPv4Address2))),
        v6_address1_(inet::IpAddress(ToFIDL(kIPv6Address1))),
        v6_address2_(inet::IpAddress(ToFIDL(kIPv6Address2))),
        v6_address_not_link_local_(inet::IpAddress(ToFIDL(kIPv6AddressNotLinkLocal))) {
    properties_ = fuchsia::net::interfaces::Properties();
    properties_.set_id(kID);
    properties_.set_name(kName);
    properties_.set_device_class(fuchsia::net::interfaces::DeviceClass::WithDevice(
        fuchsia::hardware::network::DeviceClass::WLAN));
    properties_.set_online(true);
    properties_.set_has_default_ipv4_route(false);
    properties_.set_has_default_ipv6_route(false);

    properties_.set_addresses(Addresses1());
  }

 protected:
  void SetUp() override {
    TestLoopFixture::SetUp();

    fuchsia::net::interfaces::WatcherPtr watcher;
    ASSERT_OK(binding_->Bind(watcher.NewRequest()));
    transceiver_.Start(
        std::move(watcher), []() {}, [](auto, auto) {}, StubInterfaceTransceiver::Create);
  }

  void TearDown() override {
    transceiver_.Stop();

    TestLoopFixture::TearDown();
  }

  MdnsTransceiver transceiver_;
  InterfacesWatcherImpl fake_watcher_impl_;
  const std::unique_ptr<fidl::Binding<fuchsia::net::interfaces::Watcher>> binding_;
  fuchsia::net::interfaces::Properties properties_;
  const inet::IpAddress v4_address1_, v4_address2_, v6_address1_, v6_address2_,
      v6_address_not_link_local_;
};

TEST_F(MdnsTransceiverTests, IgnoreLoopback) {
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_not_link_local_), nullptr);

  properties_.set_device_class(
      fuchsia::net::interfaces::DeviceClass::WithLoopback(fuchsia::net::interfaces::Empty()));

  RunLoopUntilIdle();

  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithExisting(std::move(properties_))));

  RunLoopUntilIdle();

  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_not_link_local_), nullptr);
}

TEST_F(MdnsTransceiverTests, OnlineChange) {
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_not_link_local_), nullptr);

  RunLoopUntilIdle();

  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithExisting(std::move(properties_))));

  RunLoopUntilIdle();

  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v4_address1_), nullptr);
  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v6_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_not_link_local_), nullptr);

  fuchsia::net::interfaces::Properties online_false;
  online_false.set_id(kID);
  online_false.set_online(false);
  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithChanged(std::move(online_false))));

  RunLoopUntilIdle();

  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_not_link_local_), nullptr);

  fuchsia::net::interfaces::Properties online_true;
  online_false.set_id(kID);
  online_false.set_online(true);
  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithChanged(std::move(online_false))));

  RunLoopUntilIdle();

  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v4_address1_), nullptr);
  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v6_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_not_link_local_), nullptr);
}

TEST_F(MdnsTransceiverTests, AddressesChange) {
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_not_link_local_), nullptr);

  RunLoopUntilIdle();

  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithExisting(std::move(properties_))));

  RunLoopUntilIdle();

  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v4_address1_), nullptr);
  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v6_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_not_link_local_), nullptr);

  fuchsia::net::interfaces::Properties addresses_change;
  addresses_change.set_id(kID);
  addresses_change.set_addresses(Addresses2());
  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithChanged(std::move(addresses_change))));

  RunLoopUntilIdle();

  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address1_), nullptr);
  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v4_address2_), nullptr);
  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v6_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_not_link_local_), nullptr);
}

TEST_F(MdnsTransceiverTests, OnlineAndAddressesChange) {
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_not_link_local_), nullptr);

  RunLoopUntilIdle();

  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithExisting(std::move(properties_))));

  RunLoopUntilIdle();

  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v4_address1_), nullptr);
  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v6_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_not_link_local_), nullptr);

  fuchsia::net::interfaces::Properties offline_and_addresses_change;
  offline_and_addresses_change.set_id(kID);
  offline_and_addresses_change.set_online(false);
  offline_and_addresses_change.set_addresses(Addresses2());

  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithChanged(std::move(offline_and_addresses_change))));

  RunLoopUntilIdle();

  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_not_link_local_), nullptr);

  fuchsia::net::interfaces::Properties online_and_addresses_change;
  online_and_addresses_change.set_id(kID);
  online_and_addresses_change.set_online(true);
  online_and_addresses_change.set_addresses(Addresses1());
  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithChanged(std::move(online_and_addresses_change))));

  RunLoopUntilIdle();

  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v4_address1_), nullptr);
  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v6_address1_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_not_link_local_), nullptr);
}

}  // namespace mdns::test
