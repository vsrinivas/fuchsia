// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net_interfaces.h"

#include <fuchsia/net/interfaces/cpp/fidl.h>

#include "lib/gtest/real_loop_fixture.h"
#include "src/lib/testing/predicates/status.h"

namespace net::interfaces::test {

constexpr std::array<uint8_t, 4> kIPv4Address1 = {1, 2, 3, 4};
constexpr std::array<uint8_t, 4> kIPv4Address2 = {5, 6, 7, 8};
constexpr std::array<uint8_t, 16> kIPv6Address1 = {0x01, 0x23, 0x45, 0x67, 0, 0, 0, 0,
                                                   0,    0,    0,    0,    0, 0, 0, 1};
constexpr std::array<uint8_t, 16> kIPv6Address2 = {0x89, 0xAB, 0xCD, 0xEF, 0, 0, 0, 0,
                                                   0,    0,    0,    0,    0, 0, 0, 1};
constexpr uint8_t kIPv4PrefixLength = 24;
constexpr uint8_t kIPv6PrefixLength = 64;
constexpr uint64_t kID = 1;
constexpr uint64_t kWrongID = 0xffffffff;
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

}  // namespace

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

    before_change_.mutable_addresses()->reserve(2);
    InitAddress(before_change_.mutable_addresses()->emplace_back(), kIPv4Address1);
    InitAddress(before_change_.mutable_addresses()->emplace_back(), kIPv6Address1);

    after_change_ = fuchsia::net::interfaces::Properties();
    after_change_.set_id(kID);
    after_change_.set_name(kName);
    after_change_.set_device_class(
        fuchsia::net::interfaces::DeviceClass::WithLoopback(fuchsia::net::interfaces::Empty()));
    after_change_.set_online(true);
    after_change_.set_has_default_ipv4_route(true);
    after_change_.set_has_default_ipv6_route(true);

    after_change_.mutable_addresses()->reserve(2);
    InitAddress(after_change_.mutable_addresses()->emplace_back(), kIPv4Address2);
    InitAddress(after_change_.mutable_addresses()->emplace_back(), kIPv6Address2);
  }

  void SetUp() override {
    EXPECT_OK(after_change_.Clone(&change_));
    change_.clear_name();
    change_.clear_device_class();

    fuchsia::net::interfaces::Properties properties1, properties2;
    EXPECT_OK(before_change_.Clone(&properties1));
    ASSERT_OK(after_change_.Clone(&properties2));

    validated_before_change_ = net::interfaces::Properties::VerifyAndCreate(std::move(properties1));
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
