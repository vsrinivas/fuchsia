// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net_interfaces.h"

#include <fuchsia/net/interfaces/cpp/fidl.h>

#include "src/lib/testing/predicates/status.h"
#include "test_common.h"

namespace net::interfaces::test {

constexpr uint64_t kLoopbackID = 1;
constexpr uint64_t kEthernetID = 2;
constexpr uint64_t kWrongID = 0xffffffff;

namespace {

net::interfaces::Properties Verify(fuchsia::net::interfaces::Properties properties) {
  auto verified = Properties::VerifyAndCreate(std::move(properties));
  EXPECT_TRUE(verified.has_value());
  return std::move(verified.value());
}

fuchsia::net::interfaces::Properties LoopbackProperties(
    bool online, bool has_default_ipv4_route, bool has_default_ipv6_route,
    std::vector<fuchsia::net::interfaces::Address> addresses) {
  fuchsia::net::interfaces::Properties properties;
  properties.set_id(kLoopbackID);
  properties.set_name(kName);
  properties.set_device_class(
      fuchsia::net::interfaces::DeviceClass::WithLoopback(fuchsia::net::interfaces::Empty()));

  SetMutableProperties(properties, online, has_default_ipv4_route, has_default_ipv6_route,
                       std::move(addresses));
  return properties;
}

fuchsia::net::interfaces::Properties EthernetProperties(
    bool online, bool has_default_ipv4_route, bool has_default_ipv6_route,
    std::vector<fuchsia::net::interfaces::Address> addresses) {
  fuchsia::net::interfaces::Properties properties;
  properties.set_id(kEthernetID);
  properties.set_name(kName);
  fuchsia::net::interfaces::DeviceClass device_class;
  device_class.set_device(fuchsia::hardware::network::DeviceClass::ETHERNET);
  properties.set_device_class(std::move(device_class));
  SetMutableProperties(properties, online, has_default_ipv4_route, has_default_ipv6_route,
                       std::move(addresses));
  return properties;
}

fuchsia::net::interfaces::Properties FullProperties() {
  return LoopbackProperties(true, true, true, Addresses(kIPv4Address1, kIPv6Address1));
}

void AddInterface(PropertiesMap& properties_map) {}

template <typename T, typename... Ts>
void AddInterface(PropertiesMap& properties_map, T interface, Ts... interfaces) {
  auto result =
      properties_map.Update(fuchsia::net::interfaces::Event::WithExisting(std::move(interface)));
  EXPECT_TRUE(result.is_ok()) << "Update error: "
                              << PropertiesMap::update_error_get_string(result.error());
  AddInterface(properties_map, interfaces...);
}

template <typename... Ts>
PropertiesMap NewPropertiesMap(Ts... interfaces) {
  PropertiesMap properties_map;
  AddInterface(properties_map, std::move(interfaces...));
  return properties_map;
}

}  // namespace

TEST(Verify, Success) {
  auto properties = FullProperties();
  auto validated_properties = Verify(FullProperties());

  EXPECT_EQ(validated_properties.id(), properties.id());
  EXPECT_EQ(validated_properties.name(), properties.name());
  EXPECT_EQ(validated_properties.online(), properties.online());
  EXPECT_EQ(validated_properties.has_default_ipv4_route(), properties.has_default_ipv4_route());
  EXPECT_EQ(validated_properties.has_default_ipv6_route(), properties.has_default_ipv6_route());
  EXPECT_TRUE(fidl::Equals(validated_properties.device_class(), properties.device_class()));
  EXPECT_TRUE(fidl::Equals(validated_properties.addresses(), properties.addresses()));
}

TEST(Verify, MissingID) {
  fuchsia::net::interfaces::Properties properties = FullProperties();
  properties.clear_id();
  ASSERT_EQ(Properties::VerifyAndCreate(std::move(properties)), std::nullopt);
}

TEST(Verify, MissingAddresses) {
  fuchsia::net::interfaces::Properties properties = FullProperties();
  properties.clear_addresses();
  ASSERT_EQ(Properties::VerifyAndCreate(std::move(properties)), std::nullopt);
}

TEST(Verify, InvalidAddress) {
  fuchsia::net::interfaces::Properties properties = FullProperties();
  // Add an address that is not initialized.
  properties.mutable_addresses()->emplace_back();
  ASSERT_EQ(Properties::VerifyAndCreate(std::move(properties)), std::nullopt);
}

TEST(Update, Success) {
  Properties properties =
      Verify(LoopbackProperties(false, false, false, Addresses(kIPv4Address1, kIPv6Address1)));

  fuchsia::net::interfaces::Properties change;
  change.set_id(kLoopbackID);
  SetMutableProperties(change, true, true, true, Addresses(kIPv4Address2, kIPv6Address2));
  ASSERT_TRUE(properties.Update(&change));
  ASSERT_EQ(properties,
            Verify(LoopbackProperties(true, true, true, Addresses(kIPv4Address2, kIPv6Address2))));

  fuchsia::net::interfaces::Properties before_change;
  SetMutableProperties(before_change, false, false, false, Addresses(kIPv4Address1, kIPv6Address1));
  EXPECT_TRUE(fidl::Equals(change, before_change));
}

TEST(Update, MissingID) {
  fuchsia::net::interfaces::Properties change;
  change.set_online(true);
  ASSERT_FALSE(Verify(LoopbackProperties(false, false, false, {})).Update(&change));
}

TEST(Update, MismatchedID) {
  fuchsia::net::interfaces::Properties change;
  change.set_id(kWrongID);
  change.set_online(true);
  ASSERT_FALSE(Verify(LoopbackProperties(false, false, false, {})).Update(&change));
}

TEST(Update, InvalidAddress) {
  fuchsia::net::interfaces::Properties change;
  change.set_id(kLoopbackID);
  change.mutable_addresses()->emplace_back();
  ASSERT_FALSE(Verify(LoopbackProperties(false, false, false, {})).Update(&change));
}

TEST(IsGloballyRoutable, Loopback) {
  EXPECT_FALSE(Verify(LoopbackProperties(true, true, true, Addresses(kIPv4Address1, kIPv6Address1)))
                   .IsGloballyRoutable());
}

TEST(IsGloballyRoutable, Offline) {
  EXPECT_FALSE(
      Verify(EthernetProperties(false, true, true, Addresses(kIPv4Address1, kIPv6Address1)))
          .IsGloballyRoutable());
}

TEST(IsGloballyRoutable, NoDefaultRoutes) {
  EXPECT_FALSE(
      Verify(EthernetProperties(true, false, false, Addresses(kIPv4Address1, kIPv6Address1)))
          .IsGloballyRoutable());
}

TEST(IsGloballyRoutable, NoAddresses) {
  EXPECT_FALSE(Verify(EthernetProperties(true, true, true, {})).IsGloballyRoutable());
}

TEST(IsGloballyRoutable, IPv6LinkLocal) {
  EXPECT_FALSE(Verify(EthernetProperties(true, false, true, Addresses(kIPv6LLAddress)))
                   .IsGloballyRoutable());
}

TEST(IsGloballyRoutable, IPv4AddressAndDefaultRoute) {
  EXPECT_TRUE(
      Verify(EthernetProperties(true, true, false, Addresses(kIPv4Address1))).IsGloballyRoutable());
}

TEST(IsGloballyRoutable, IPv6GlobalAddressAndDefaultRoute) {
  EXPECT_TRUE(
      Verify(EthernetProperties(true, false, true, Addresses(kIPv6Address1))).IsGloballyRoutable());
}

TEST(PropertiesMap, Success) {
  PropertiesMap properties_map;
  std::unordered_map<uint64_t, Properties> want;

  auto result =
      properties_map.Update(fuchsia::net::interfaces::Event::WithExisting(FullProperties()));
  ASSERT_TRUE(result.is_ok()) << "Update error: "
                              << PropertiesMap::update_error_get_string(result.error());
  want.emplace(kLoopbackID, Verify(FullProperties()));
  ASSERT_EQ(properties_map.properties_map(), want);

  fuchsia::net::interfaces::Properties want_properties,
      ethernet_properties =
          EthernetProperties(false, false, false, Addresses(kIPv4Address1, kIPv6Address1));
  ASSERT_OK(ethernet_properties.Clone(&want_properties));
  result = properties_map.Update(
      fuchsia::net::interfaces::Event::WithAdded(std::move(ethernet_properties)));
  ASSERT_TRUE(result.is_ok()) << "Update error: "
                              << PropertiesMap::update_error_get_string(result.error());
  want.emplace(kEthernetID, Verify(std::move(want_properties)));
  ASSERT_EQ(properties_map.properties_map(), want);

  fuchsia::net::interfaces::Properties change;
  change.set_id(kEthernetID);
  SetMutableProperties(change, true, true, true, Addresses(kIPv4Address2, kIPv6Address2));
  result = properties_map.Update(fuchsia::net::interfaces::Event::WithChanged(std::move(change)));
  ASSERT_TRUE(result.is_ok()) << "Update error: "
                              << PropertiesMap::update_error_get_string(result.error());
  auto it = want.find(kEthernetID);
  ASSERT_NE(it, want.end());
  it->second =
      Verify(EthernetProperties(true, true, true, Addresses(kIPv4Address2, kIPv6Address2)));
  ASSERT_EQ(properties_map.properties_map(), want);

  uint64_t id = kLoopbackID;
  result = properties_map.Update(fuchsia::net::interfaces::Event::WithRemoved(std::move(id)));
  ASSERT_TRUE(result.is_ok()) << "Update error: "
                              << PropertiesMap::update_error_get_string(result.error());
  want.erase(kLoopbackID);
  ASSERT_EQ(properties_map.properties_map(), want);
}

TEST(PropertiesMap, InvalidExisting) {
  PropertiesMap properties_map;

  auto properties = FullProperties();
  properties.clear_id();

  auto result =
      properties_map.Update(fuchsia::net::interfaces::Event::WithExisting(std::move(properties)));
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(result.error(), PropertiesMap::UpdateErrorVariant(PropertiesMap::UpdateError(
                                PropertiesMap::UpdateError::kInvalidExisting)));
}

TEST(PropertiesMap, InvalidAdded) {
  auto properties = FullProperties();
  properties.clear_id();

  auto result =
      PropertiesMap().Update(fuchsia::net::interfaces::Event::WithAdded(std::move(properties)));
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(result.error(), PropertiesMap::UpdateErrorVariant(PropertiesMap::UpdateError(
                                PropertiesMap::UpdateError::kInvalidAdded)));
}

TEST(PropertiesMap, MissingId) {
  fuchsia::net::interfaces::Properties change_missing_id;
  change_missing_id.set_online(true);

  auto result =
      NewPropertiesMap(LoopbackProperties(false, false, false, {}))
          .Update(fuchsia::net::interfaces::Event::WithChanged(std::move(change_missing_id)));
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(result.error(), PropertiesMap::UpdateErrorVariant(PropertiesMap::UpdateError(
                                PropertiesMap::UpdateError::kMissingId)));
}

TEST(PropertiesMap, InvalidChanged) {
  fuchsia::net::interfaces::Properties change_invalid_address;
  change_invalid_address.set_id(kLoopbackID);
  change_invalid_address.mutable_addresses()->emplace_back();

  auto result =
      NewPropertiesMap(LoopbackProperties(false, false, false, {}))
          .Update(fuchsia::net::interfaces::Event::WithChanged(std::move(change_invalid_address)));
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(result.error(), PropertiesMap::UpdateErrorVariant(PropertiesMap::UpdateError(
                                PropertiesMap::UpdateError::kInvalidChanged)));
}

TEST(PropertiesMap, InvalidEvent) {
  auto result = PropertiesMap().Update(fuchsia::net::interfaces::Event());
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(result.error(), PropertiesMap::UpdateErrorVariant(PropertiesMap::UpdateError(
                                PropertiesMap::UpdateError::kInvalidEvent)));
}

TEST(PropertiesMap, DuplicateExisting) {
  auto result = NewPropertiesMap(FullProperties())
                    .Update(fuchsia::net::interfaces::Event::WithExisting(FullProperties()));
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(result.error(),
            PropertiesMap::UpdateErrorVariant(
                PropertiesMap::UpdateErrorWithId<
                    PropertiesMap::UpdateErrorWithIdKind::kDuplicateExisting>{.id = kLoopbackID}));
}

TEST(PropertiesMap, DuplicateAdded) {
  auto result = NewPropertiesMap(FullProperties())
                    .Update(fuchsia::net::interfaces::Event::WithAdded(FullProperties()));
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(
      result.error(),
      PropertiesMap::UpdateErrorVariant(
          PropertiesMap::UpdateErrorWithId<PropertiesMap::UpdateErrorWithIdKind::kDuplicateAdded>{
              .id = kLoopbackID}));
}

TEST(PropertiesMap, UnknownChanged) {
  fuchsia::net::interfaces::Properties change_unknown;
  change_unknown.set_id(kWrongID);
  change_unknown.set_online(true);
  auto result = PropertiesMap().Update(
      fuchsia::net::interfaces::Event::WithChanged(std::move(change_unknown)));
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(
      result.error(),
      PropertiesMap::UpdateErrorVariant(
          PropertiesMap::UpdateErrorWithId<PropertiesMap::UpdateErrorWithIdKind::kUnknownChanged>{
              .id = kWrongID}));
}

TEST(PropertiesMap, UnknownRemoved) {
  uint64_t id = kWrongID;
  auto result = PropertiesMap().Update(fuchsia::net::interfaces::Event::WithRemoved(std::move(id)));
  ASSERT_TRUE(result.is_error());
  ASSERT_EQ(
      result.error(),
      PropertiesMap::UpdateErrorVariant(
          PropertiesMap::UpdateErrorWithId<PropertiesMap::UpdateErrorWithIdKind::kUnknownRemoved>{
              .id = kWrongID}));
}

class ReachabilityWatcherTest : public gtest::TestLoopFixture {
 public:
  ReachabilityWatcherTest() : ReachabilityWatcherTest(fuchsia::net::interfaces::WatcherPtr()) {}

  void AssertReachable(bool want) {
    ASSERT_TRUE(reachable_.has_value());
    ASSERT_TRUE(reachable_.value().is_ok());
    ASSERT_EQ(reachable_.value().value(), want);
    reachable_.reset();
  }

 protected:
  std::optional<fpromise::result<bool, ReachabilityWatcher::ErrorVariant>> reachable_;

  FakeWatcherImpl fake_watcher_impl_;
  std::unique_ptr<fidl::Binding<fuchsia::net::interfaces::Watcher>> watcher_binding_;
  ReachabilityWatcher reachability_watcher_;

 private:
  ReachabilityWatcherTest(fuchsia::net::interfaces::WatcherPtr watcher)
      : watcher_binding_(std::make_unique<fidl::Binding<fuchsia::net::interfaces::Watcher>>(
            &fake_watcher_impl_, watcher.NewRequest())),
        reachability_watcher_(std::move(watcher),
                              [this](auto reachable) { reachable_ = reachable; }) {}
};

TEST_F(ReachabilityWatcherTest, Basic) {
  constexpr uint64_t kID1 = 1;
  constexpr uint64_t kID2 = 2;

  RunLoopUntilIdle();

  fake_watcher_impl_.SendExistingEvent(kID1, false);
  RunLoopUntilIdle();
  ASSERT_NO_FATAL_FAILURE(AssertReachable(false));

  fake_watcher_impl_.SendAddedEvent(kID2, true);
  RunLoopUntilIdle();
  ASSERT_NO_FATAL_FAILURE(AssertReachable(true));

  fake_watcher_impl_.SendChangedEvent(kID1, true);
  RunLoopUntilIdle();
  ASSERT_FALSE(reachable_.has_value())
      << "got: "
      << (reachable_.value().is_ok()
              ? std::to_string(reachable_.value().value())
              : ReachabilityWatcher::error_get_string(reachable_.value().take_error()));

  fake_watcher_impl_.SendChangedEvent(kID2, false);
  RunLoopUntilIdle();
  ASSERT_FALSE(reachable_.has_value())
      << "got: "
      << (reachable_.value().is_ok()
              ? std::to_string(reachable_.value().value())
              : ReachabilityWatcher::error_get_string(reachable_.value().take_error()));

  fake_watcher_impl_.SendRemovedEvent(kID1);
  RunLoopUntilIdle();
  ASSERT_NO_FATAL_FAILURE(AssertReachable(false));
}

TEST_F(ReachabilityWatcherTest, MalformedEvent) {
  RunLoopUntilIdle();

  fake_watcher_impl_.SendExistingEvent(kEthernetID, true);
  RunLoopUntilIdle();
  ASSERT_NO_FATAL_FAILURE(AssertReachable(true));

  fake_watcher_impl_.SendEvent(
      fuchsia::net::interfaces::Event::WithExisting(fuchsia::net::interfaces::Properties()));
  RunLoopUntilIdle();
  ASSERT_TRUE(reachable_.has_value());
  ASSERT_TRUE(reachable_.value().is_error());
  ASSERT_EQ(reachable_.value().take_error(),
            ReachabilityWatcher::ErrorVariant(PropertiesMap::UpdateErrorVariant(
                PropertiesMap::UpdateError(PropertiesMap::UpdateError::kInvalidExisting))));
}

TEST_F(ReachabilityWatcherTest, ChannelClose) {
  RunLoopUntilIdle();

  fake_watcher_impl_.SendExistingEvent(kEthernetID, true);
  RunLoopUntilIdle();
  ASSERT_NO_FATAL_FAILURE(AssertReachable(true));

  watcher_binding_->Close(ZX_OK);
  RunLoopUntilIdle();
  ASSERT_TRUE(reachable_.has_value());
  ASSERT_TRUE(reachable_.value().is_error());
  ASSERT_EQ(reachable_.value().take_error(),
            ReachabilityWatcher::ErrorVariant(ReachabilityWatcher::Error::kChannelClosed));
}

}  // namespace net::interfaces::test
