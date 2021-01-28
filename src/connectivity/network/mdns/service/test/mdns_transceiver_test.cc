// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/mdns_transceiver.h"

#include <fuchsia/hardware/network/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/connectivity/network/mdns/service/mdns_addresses.h"
#include "src/connectivity/network/mdns/service/mdns_fidl_util.h"
#include "src/lib/testing/predicates/status.h"

namespace mdns::test {

constexpr uint8_t kID = 1;
constexpr const char* kName = "test01";
constexpr uint8_t kIPv4Address[4] = {1, 2, 3, 1};
constexpr uint8_t kIPv4Address2[4] = {4, 5, 6, 1};
constexpr uint8_t kIPv6Address[16] = {0x01, 0x23, 0x45, 0x67, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
constexpr uint8_t kIPv6Address2[16] = {0x89, 0xAB, 0xCD, 0xEF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
constexpr uint8_t kIPv4PrefixLength = 24;
constexpr uint8_t kIPv6PrefixLength = 64;

namespace {

fuchsia::net::interfaces::Address NewV4Address(const uint8_t* bytes) {
  fuchsia::net::Ipv4Address v4_addr;
  std::copy_n(bytes, v4_addr.addr.size(), v4_addr.addr.begin());

  fuchsia::net::Subnet v4_subnet;
  v4_subnet.addr.set_ipv4(v4_addr);
  v4_subnet.prefix_len = kIPv4PrefixLength;

  fuchsia::net::interfaces::Address v4_if_addr;
  v4_if_addr.set_addr(std::move(v4_subnet));
  return v4_if_addr;
}

fuchsia::net::interfaces::Address NewV6Address(const uint8_t* bytes) {
  fuchsia::net::Ipv6Address v6_addr;
  std::copy_n(bytes, v6_addr.addr.size(), v6_addr.addr.begin());

  fuchsia::net::Subnet v6_subnet;
  v6_subnet.addr.set_ipv6(v6_addr);
  v6_subnet.prefix_len = kIPv6PrefixLength;

  fuchsia::net::interfaces::Address v6_if_addr;
  v6_if_addr.set_addr(std::move(v6_subnet));
  return v6_if_addr;
}

}  // namespace

class InterfacesWatcherImpl : public fuchsia::net::interfaces::testing::Watcher_TestBase {
 public:
  void NotImplemented_(const std::string& name) override {
    std::cout << "Not implemented: " << name << std::endl;
  }

  void Watch(WatchCallback callback) override {
    FX_DCHECK(!watch_cb_.has_value());
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

  std::optional<fuchsia::net::interfaces::Watcher::WatchCallback> GetWatchCallback() {
    std::optional<fuchsia::net::interfaces::Watcher::WatchCallback> rtn;
    rtn.swap(watch_cb_);
    return rtn;
  }

  std::optional<WatchCallback> watch_cb_;
};

class StubInterfaceTransceiver : public MdnsInterfaceTransceiver {
 public:
  StubInterfaceTransceiver(inet::IpAddress address, const std::string& name, uint32_t index,
                           Media media)
      : MdnsInterfaceTransceiver(address, name, index, media) {}

  static std::unique_ptr<MdnsInterfaceTransceiver> Create(inet::IpAddress address,
                                                          const std::string& name, uint32_t index,
                                                          Media media) {
    return std::make_unique<StubInterfaceTransceiver>(address, name, index, media);
  }

 protected:
  int SetOptionDisableMulticastLoop() override { return 0; }
  int SetOptionJoinMulticastGroup() override { return 0; }
  int SetOptionOutboundInterface() override { return 0; }
  int SetOptionUnicastTtl() override { return 0; }
  int SetOptionMulticastTtl() override { return 0; }
  int SetOptionFamilySpecific() override { return 0; }
  int Bind() override { return 0; }
  int SendTo(const void* buffer, size_t size, const inet::SocketAddress& address) override {
    return 0;
  }

  bool Start(const MdnsAddresses& addresses, InboundMessageCallback callback) override {
    return true;
  }
  void Stop() override{};
};

class MdnsTransceiverTests : public gtest::TestLoopFixture {
 public:
  MdnsTransceiverTests() {
    properties_ = fuchsia::net::interfaces::Properties();
    properties_.set_id(kID);
    properties_.set_name(kName);
    properties_.set_device_class(fuchsia::net::interfaces::DeviceClass::WithDevice(
        fuchsia::hardware::network::DeviceClass::WLAN));
    properties_.set_online(true);
    properties_.set_has_default_ipv4_route(false);
    properties_.set_has_default_ipv6_route(false);

    std::vector<fuchsia::net::interfaces::Address> addresses;
    addresses.reserve(2);
    addresses.push_back(NewV4Address(kIPv4Address));
    addresses.push_back(NewV6Address(kIPv6Address));
    properties_.set_addresses(std::move(addresses));

    addresses_.reserve(2);
    addresses_.push_back(NewV4Address(kIPv4Address));
    addresses_.push_back(NewV6Address(kIPv6Address));

    addresses2_.reserve(2);
    addresses2_.push_back(NewV4Address(kIPv4Address2));
    addresses2_.push_back(NewV6Address(kIPv6Address2));

    fuchsia::net::Ipv4Address v4_addr, v4_addr2;
    std::copy(std::begin(kIPv4Address), std::end(kIPv4Address), v4_addr.addr.begin());
    std::copy(std::begin(kIPv4Address2), std::end(kIPv4Address2), v4_addr2.addr.begin());
    v4_address_ = inet::IpAddress(&v4_addr);
    v4_address2_ = inet::IpAddress(&v4_addr2);

    fuchsia::net::Ipv6Address v6_addr, v6_addr2;
    std::copy(std::begin(kIPv6Address), std::end(kIPv6Address), v6_addr.addr.begin());
    std::copy(std::begin(kIPv6Address2), std::end(kIPv6Address2), v6_addr2.addr.begin());
    v6_address_ = inet::IpAddress(&v6_addr);
    v6_address2_ = inet::IpAddress(&v6_addr2);

    binding_ =
        std::make_unique<fidl::Binding<fuchsia::net::interfaces::Watcher>>(&fake_watcher_impl_);
  }

  void SetUp() override {
    TestLoopFixture::SetUp();

    fuchsia::net::interfaces::WatcherPtr watcher;
    ASSERT_OK(binding_->Bind(watcher.NewRequest()));
    transceiver_.Start(
        std::move(watcher), MdnsAddresses(), []() {}, [](auto, auto) {},
        StubInterfaceTransceiver::Create);
  }

  void TearDown() override {
    transceiver_.Stop();

    TestLoopFixture::TearDown();
  }

 protected:
  MdnsTransceiver transceiver_;
  InterfacesWatcherImpl fake_watcher_impl_;
  std::unique_ptr<fidl::Binding<fuchsia::net::interfaces::Watcher>> binding_;
  fuchsia::net::interfaces::Properties properties_;
  std::vector<fuchsia::net::interfaces::Address> addresses_, addresses2_;
  inet::IpAddress v4_address_, v4_address2_, v6_address_, v6_address2_;
};

TEST_F(MdnsTransceiverTests, IgnoreLoopback) {
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_), nullptr);

  properties_.set_device_class(
      fuchsia::net::interfaces::DeviceClass::WithLoopback(fuchsia::net::interfaces::Empty()));

  RunLoopUntilIdle();

  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithExisting(std::move(properties_))));

  RunLoopUntilIdle();

  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_), nullptr);
}

TEST_F(MdnsTransceiverTests, OnlineChange) {
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_), nullptr);

  RunLoopUntilIdle();

  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithExisting(std::move(properties_))));

  RunLoopUntilIdle();

  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v4_address_), nullptr);
  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v6_address_), nullptr);

  fuchsia::net::interfaces::Properties online_false;
  online_false.set_id(kID);
  online_false.set_online(false);
  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithChanged(std::move(online_false))));

  RunLoopUntilIdle();

  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_), nullptr);

  fuchsia::net::interfaces::Properties online_true;
  online_false.set_id(kID);
  online_false.set_online(true);
  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithChanged(std::move(online_false))));

  RunLoopUntilIdle();

  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v4_address_), nullptr);
  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v6_address_), nullptr);
}

TEST_F(MdnsTransceiverTests, AddressesChange) {
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address2_), nullptr);

  RunLoopUntilIdle();

  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithExisting(std::move(properties_))));

  RunLoopUntilIdle();

  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v4_address_), nullptr);
  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v6_address_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address2_), nullptr);

  fuchsia::net::interfaces::Properties addresses_change;
  addresses_change.set_id(kID);
  addresses_change.set_addresses(std::move(addresses2_));
  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithChanged(std::move(addresses_change))));

  RunLoopUntilIdle();

  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_), nullptr);
  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v4_address2_), nullptr);
  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v6_address2_), nullptr);
}

TEST_F(MdnsTransceiverTests, OnlineAndAddressesChange) {
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address2_), nullptr);

  RunLoopUntilIdle();

  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithExisting(std::move(properties_))));

  RunLoopUntilIdle();

  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v4_address_), nullptr);
  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v6_address_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address2_), nullptr);

  fuchsia::net::interfaces::Properties offline_and_addresses_change;
  offline_and_addresses_change.set_id(kID);
  offline_and_addresses_change.set_online(false);
  offline_and_addresses_change.set_addresses(std::move(addresses2_));

  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithChanged(std::move(offline_and_addresses_change))));

  RunLoopUntilIdle();

  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address2_), nullptr);

  fuchsia::net::interfaces::Properties online_and_addresses_change;
  online_and_addresses_change.set_id(kID);
  online_and_addresses_change.set_online(true);
  online_and_addresses_change.set_addresses(std::move(addresses_));
  ASSERT_TRUE(fake_watcher_impl_.CompleteWatchCallback(
      fuchsia::net::interfaces::Event::WithChanged(std::move(online_and_addresses_change))));

  RunLoopUntilIdle();

  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v4_address_), nullptr);
  EXPECT_NE(transceiver_.GetInterfaceTransceiver(v6_address_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v4_address2_), nullptr);
  EXPECT_EQ(transceiver_.GetInterfaceTransceiver(v6_address2_), nullptr);
}

}  // namespace mdns::test
