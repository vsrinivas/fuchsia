// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_provider/impl/p2p_provider_impl.h"

#include <lib/fit/function.h>

#include <algorithm>
#include <ostream>
#include <string>

// gtest matchers are in gmock.
#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/test_loop_fixture.h>

#include "fuchsia/netconnector/cpp/fidl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/p2p_provider/impl/static_user_id_provider.h"
#include "src/ledger/bin/p2p_provider/public/user_id_provider.h"
#include "src/ledger/bin/testing/netconnector/netconnector_factory.h"
#include "src/lib/fxl/macros.h"

namespace p2p_provider {
namespace {

class RecordingClient : public P2PProvider::Client {
 public:
  struct DeviceChange {
    std::string device;
    DeviceChangeType change;

    bool operator==(const DeviceChange& b) const {
      return device == b.device && change == b.change;
    }
  };

  struct Message {
    std::string source;
    std::string data;

    bool operator==(const Message& b) const {
      return source == b.source && data == b.data;
    }
  };

  void OnDeviceChange(fxl::StringView device_name,
                      DeviceChangeType change_type) override {
    device_changes.push_back(DeviceChange{device_name.ToString(), change_type});
  }

  void OnNewMessage(fxl::StringView device_name,
                    fxl::StringView message) override {
    messages.push_back(Message{device_name.ToString(), message.ToString()});
  }
  std::vector<DeviceChange> device_changes;
  std::vector<Message> messages;
};

std::ostream& operator<<(std::ostream& os,
                         const RecordingClient::DeviceChange& d) {
  return os << "DeviceChange{" << d.device << ", " << bool(d.change) << "}";
}

std::ostream& operator<<(std::ostream& os, const RecordingClient::Message& m) {
  return os << "Message{" << m.source << ", " << m.data << "}";
}

class P2PProviderImplTest : public gtest::TestLoopFixture {
 public:
  P2PProviderImplTest() {}
  ~P2PProviderImplTest() override {}

  std::unique_ptr<P2PProvider> GetProvider(std::string host_name,
                                           std::string user_name = "user") {
    fuchsia::netconnector::NetConnectorPtr netconnector;
    net_connector_factory_.AddBinding(host_name, netconnector.NewRequest());
    return std::make_unique<p2p_provider::P2PProviderImpl>(
        std::move(host_name), std::move(netconnector),
        std::make_unique<StaticUserIdProvider>(std::move(user_name)));
  }

 protected:
  void SetUp() override { ::testing::Test::SetUp(); }

  ledger::NetConnectorFactory net_connector_factory_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(P2PProviderImplTest);
};

TEST_F(P2PProviderImplTest, ThreeHosts_SameUser) {
  std::unique_ptr<P2PProvider> provider1 = GetProvider("host1");
  RecordingClient client1;
  provider1->Start(&client1);
  RunLoopUntilIdle();
  EXPECT_TRUE(client1.device_changes.empty());

  std::unique_ptr<P2PProvider> provider2 = GetProvider("host2");
  RecordingClient client2;
  provider2->Start(&client2);
  RunLoopUntilIdle();

  EXPECT_THAT(client1.device_changes,
              testing::UnorderedElementsAre(RecordingClient::DeviceChange{
                  "host2", DeviceChangeType::NEW}));
  EXPECT_THAT(client2.device_changes,
              testing::UnorderedElementsAre(RecordingClient::DeviceChange{
                  "host1", DeviceChangeType::NEW}));

  std::unique_ptr<P2PProvider> provider3 = GetProvider("host3");
  RecordingClient client3;
  provider3->Start(&client3);
  RunLoopUntilIdle();

  EXPECT_THAT(
      client1.device_changes,
      testing::ElementsAre(
          RecordingClient::DeviceChange{"host2", DeviceChangeType::NEW},
          RecordingClient::DeviceChange{"host3", DeviceChangeType::NEW}));

  EXPECT_THAT(
      client2.device_changes,
      testing::ElementsAre(
          RecordingClient::DeviceChange{"host1", DeviceChangeType::NEW},
          RecordingClient::DeviceChange{"host3", DeviceChangeType::NEW}));

  EXPECT_THAT(
      client3.device_changes,
      testing::UnorderedElementsAre(
          RecordingClient::DeviceChange{"host1", DeviceChangeType::NEW},
          RecordingClient::DeviceChange{"host2", DeviceChangeType::NEW}));

  // Disconnect one host, and verify disconnection notices are sent.
  provider2.reset();
  RunLoopUntilIdle();

  EXPECT_THAT(
      client1.device_changes,
      testing::ElementsAre(
          RecordingClient::DeviceChange{"host2", DeviceChangeType::NEW},
          RecordingClient::DeviceChange{"host3", DeviceChangeType::NEW},
          RecordingClient::DeviceChange{"host2", DeviceChangeType::DELETED}));

  EXPECT_THAT(
      client3.device_changes,
      testing::UnorderedElementsAre(
          RecordingClient::DeviceChange{"host1", DeviceChangeType::NEW},
          RecordingClient::DeviceChange{"host2", DeviceChangeType::NEW},
          RecordingClient::DeviceChange{"host2", DeviceChangeType::DELETED}));
}

TEST_F(P2PProviderImplTest, FourHosts_TwoUsers) {
  std::unique_ptr<P2PProvider> provider1 = GetProvider("host1", "user1");
  RecordingClient client1;
  provider1->Start(&client1);
  std::unique_ptr<P2PProvider> provider2 = GetProvider("host2", "user2");
  RecordingClient client2;
  provider2->Start(&client2);
  std::unique_ptr<P2PProvider> provider3 = GetProvider("host3", "user2");
  RecordingClient client3;
  provider3->Start(&client3);
  std::unique_ptr<P2PProvider> provider4 = GetProvider("host4", "user1");
  RecordingClient client4;
  provider4->Start(&client4);
  RunLoopUntilIdle();

  // Verify that only devices with the same user connect together.
  EXPECT_THAT(client1.device_changes,
              testing::ElementsAre(RecordingClient::DeviceChange{
                  "host4", DeviceChangeType::NEW}));
  EXPECT_THAT(client2.device_changes,
              testing::ElementsAre(RecordingClient::DeviceChange{
                  "host3", DeviceChangeType::NEW}));
  EXPECT_THAT(client3.device_changes,
              testing::ElementsAre(RecordingClient::DeviceChange{
                  "host2", DeviceChangeType::NEW}));
  EXPECT_THAT(client4.device_changes,
              testing::ElementsAre(RecordingClient::DeviceChange{
                  "host1", DeviceChangeType::NEW}));

  provider4.reset();
  RunLoopUntilIdle();

  EXPECT_THAT(
      client1.device_changes,
      testing::ElementsAre(
          RecordingClient::DeviceChange{"host4", DeviceChangeType::NEW},
          RecordingClient::DeviceChange{"host4", DeviceChangeType::DELETED}));
  EXPECT_THAT(client2.device_changes,
              testing::ElementsAre(RecordingClient::DeviceChange{
                  "host3", DeviceChangeType::NEW}));
  EXPECT_THAT(client3.device_changes,
              testing::ElementsAre(RecordingClient::DeviceChange{
                  "host2", DeviceChangeType::NEW}));
}

TEST_F(P2PProviderImplTest, TwoHosts_Messages) {
  std::unique_ptr<P2PProvider> provider1 = GetProvider("host1");
  RecordingClient client1;
  provider1->Start(&client1);
  std::unique_ptr<P2PProvider> provider2 = GetProvider("host2");
  RecordingClient client2;
  provider2->Start(&client2);

  RunLoopUntilIdle();

  EXPECT_TRUE(provider1->SendMessage("host2", "datagram"));
  RunLoopUntilIdle();

  EXPECT_THAT(client1.messages, testing::ElementsAre());
  EXPECT_THAT(client2.messages, testing::ElementsAre(RecordingClient::Message{
                                    "host1", "datagram"}));
}

class MockNetConnector : public fuchsia::netconnector::NetConnector {
 public:
  explicit MockNetConnector(
      fidl::InterfaceRequest<fuchsia::netconnector::NetConnector> request)
      : binding_(this, std::move(request)) {}

  void RegisterServiceProvider(
      std::string service_name,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> service_provider)
      override {
    FXL_NOTIMPLEMENTED();
  }

  void GetDeviceServiceProvider(
      std::string device_name,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> service_provider)
      override {
    device_requests.push_back(std::move(device_name));
  }

  void GetKnownDeviceNames(uint64_t version_last_seen,
                           GetKnownDeviceNamesCallback callback) override {
    device_names_callbacks.emplace_back(version_last_seen, std::move(callback));
  }

  std::vector<std::string> device_requests;
  std::vector<std::pair<uint64_t, GetKnownDeviceNamesCallback>>
      device_names_callbacks;

 private:
  fidl::Binding<fuchsia::netconnector::NetConnector> binding_;
};

// Verifies that P2PProviderImpl does not do symmetrical connections
TEST_F(P2PProviderImplTest, HostConnectionOrdering) {
  fuchsia::netconnector::NetConnectorPtr netconnector_ptr_0;
  MockNetConnector netconnector_impl_0(netconnector_ptr_0.NewRequest());
  auto p2p_provider_0 = std::make_unique<p2p_provider::P2PProviderImpl>(
      "device0", std::move(netconnector_ptr_0),
      std::make_unique<StaticUserIdProvider>("user"));

  RecordingClient client1;
  p2p_provider_0->Start(&client1);

  RunLoopUntilIdle();

  EXPECT_EQ(1U, netconnector_impl_0.device_names_callbacks.size());

  auto request1 = std::move(netconnector_impl_0.device_names_callbacks[0]);
  request1.second(1, {"device1"});

  RunLoopUntilIdle();

  fuchsia::netconnector::NetConnectorPtr netconnector_ptr_1;
  MockNetConnector netconnector_impl_1(netconnector_ptr_1.NewRequest());
  auto p2p_provider_1 = std::make_unique<p2p_provider::P2PProviderImpl>(
      "device1", std::move(netconnector_ptr_1),
      std::make_unique<StaticUserIdProvider>("user"));

  RecordingClient client2;
  p2p_provider_1->Start(&client2);

  RunLoopUntilIdle();

  EXPECT_EQ(1U, netconnector_impl_1.device_names_callbacks.size());

  auto request2 = std::move(netconnector_impl_1.device_names_callbacks[0]);
  request2.second(1, {"device0"});

  RunLoopUntilIdle();

  // Only one device should initiate the connection. We don't really care which
  // one, as long as it is reliably correct.
  EXPECT_EQ(1U, netconnector_impl_0.device_requests.size() +
                    netconnector_impl_1.device_requests.size());
}

}  // namespace
}  // namespace p2p_provider
