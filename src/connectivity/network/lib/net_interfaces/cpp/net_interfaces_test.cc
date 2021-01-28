// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/lib/net_interfaces/cpp/net_interfaces.h"

#include <fuchsia/net/interfaces/cpp/fidl.h>

#include "lib/gtest/real_loop_fixture.h"
#include "src/lib/testing/predicates/status.h"

namespace net::interfaces::test {

constexpr uint8_t kIPv4Address[4] = {1, 2, 3, 4};
constexpr uint8_t kIPv4Address2[4] = {5, 6, 7, 8};
constexpr uint8_t kIPv6Address[16] = {0x01, 0x23, 0x45, 0x67, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
constexpr uint8_t kIPv6Address2[16] = {0x89, 0xAB, 0xCD, 0xEF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
constexpr uint8_t kIPv4PrefixLength = 24;
constexpr uint8_t kIPv6PrefixLength = 64;
constexpr uint64_t kID = 1;
constexpr uint64_t kWrongID = 0xffffffff;
constexpr const char* const kName = "test01";

class PropertiesTest : public gtest::RealLoopFixture {
 public:
  PropertiesTest() {
    before_change_ = fuchsia::net::interfaces::Properties();
    before_change_.set_id(kID);
    before_change_.set_name(kName);
    before_change_.set_device_class(
        fuchsia::net::interfaces::DeviceClass::WithLoopback(fuchsia::net::interfaces::Empty()));
    before_change_.set_online(false);
    before_change_.set_has_default_ipv4_route(false);
    before_change_.set_has_default_ipv6_route(false);

    std::vector<fuchsia::net::interfaces::Address> addresses_before_change;
    addresses_before_change.reserve(2);
    addresses_before_change.push_back(NewV4Address(kIPv4Address));
    addresses_before_change.push_back(NewV6Address(kIPv6Address));
    before_change_.set_addresses(std::move(addresses_before_change));

    after_change_ = fuchsia::net::interfaces::Properties();
    after_change_.set_id(kID);
    after_change_.set_name(kName);
    after_change_.set_device_class(
        fuchsia::net::interfaces::DeviceClass::WithLoopback(fuchsia::net::interfaces::Empty()));
    after_change_.set_online(true);
    after_change_.set_has_default_ipv4_route(true);
    after_change_.set_has_default_ipv6_route(true);

    std::vector<fuchsia::net::interfaces::Address> addresses_after_change;
    addresses_after_change.reserve(2);
    addresses_after_change.push_back(NewV4Address(kIPv4Address2));
    addresses_after_change.push_back(NewV6Address(kIPv6Address2));
    after_change_.set_addresses(std::move(addresses_after_change));
  }

  void SetUp() override {
    EXPECT_OK(after_change_.Clone(&change_));
    change_.clear_name();
    change_.clear_device_class();

    fuchsia::net::interfaces::Properties properties, properties2;
    EXPECT_OK(before_change_.Clone(&properties));
    ASSERT_OK(after_change_.Clone(&properties2));

    validated_before_change_ = net::interfaces::Properties::VerifyAndCreate(std::move(properties));
    validated_after_change_ = net::interfaces::Properties::VerifyAndCreate(std::move(properties2));
    EXPECT_TRUE(validated_before_change_.has_value());
    ASSERT_TRUE(validated_after_change_.has_value());
  }

 protected:
  fuchsia::net::interfaces::Properties before_change_, after_change_;
  // The change which, if applied to |before_change_|, will result in properties equal to
  // |after_change_|.
  fuchsia::net::interfaces::Properties change_;
  // The SetUp method guarantees that these will have a value in them.
  std::optional<net::interfaces::Properties> validated_before_change_, validated_after_change_;

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
};

void expect_equal(const net::interfaces::Properties& validated_properties,
                  const fuchsia::net::interfaces::Properties properties) {
  EXPECT_EQ(validated_properties.id(), properties.id());
  EXPECT_EQ(validated_properties.name(), properties.name());
  EXPECT_EQ(validated_properties.online(), properties.online());
  EXPECT_EQ(validated_properties.has_default_ipv4_route(), properties.has_default_ipv4_route());
  EXPECT_EQ(validated_properties.has_default_ipv6_route(), properties.has_default_ipv6_route());
  EXPECT_TRUE(fidl::Equals(validated_properties.device_class(), properties.device_class()));
  EXPECT_TRUE(fidl::Equals(validated_properties.addresses(), properties.addresses()));
}

TEST_F(PropertiesTest, VerifySuccess) {
  expect_equal(*validated_before_change_, std::move(before_change_));
  expect_equal(*validated_after_change_, std::move(after_change_));
}

TEST_F(PropertiesTest, VerifyMissingID) {
  before_change_.clear_id();
  ASSERT_FALSE(net::interfaces::Properties::VerifyAndCreate(std::move(before_change_)).has_value());
}

TEST_F(PropertiesTest, VerifyMissingAddresses) {
  before_change_.clear_addresses();
  ASSERT_FALSE(net::interfaces::Properties::VerifyAndCreate(std::move(before_change_)).has_value());
}

TEST_F(PropertiesTest, VerifyInvalidAddress) {
  (*before_change_.mutable_addresses())[0].clear_addr();
  ASSERT_FALSE(net::interfaces::Properties::VerifyAndCreate(std::move(before_change_)).has_value());
}

TEST_F(PropertiesTest, UpdateSuccess) {
  ASSERT_TRUE(validated_before_change_->Update(&change_));
  expect_equal(*validated_before_change_, std::move(after_change_));

  // |change_| should have been modified to contain values for all mutable fields in the original
  // |before_change_|.
  before_change_.clear_id();
  before_change_.clear_name();
  before_change_.clear_device_class();
  EXPECT_TRUE(fidl::Equals(change_, before_change_));
}

TEST_F(PropertiesTest, UpdateMissingID) {
  change_.clear_id();
  ASSERT_FALSE(validated_before_change_->Update(&change_));
}

TEST_F(PropertiesTest, UpdateMismatchedID) {
  change_.set_id(kWrongID);
  ASSERT_FALSE(validated_before_change_->Update(&change_));
}

TEST_F(PropertiesTest, UpdateInvalidAddress) {
  (*change_.mutable_addresses())[0].clear_addr();
  ASSERT_FALSE(validated_before_change_->Update(&change_));
}
}  // namespace net::interfaces::test
