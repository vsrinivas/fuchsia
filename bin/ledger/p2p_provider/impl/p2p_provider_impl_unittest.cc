// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/p2p_provider/impl/p2p_provider_impl.h"

#include <algorithm>
#include <ostream>
#include <string>

// gtest matchers are in gmock.
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/gtest/test_loop_fixture.h"
#include "peridot/bin/ledger/p2p_provider/public/user_id_provider.h"
#include "peridot/bin/ledger/testing/netconnector/netconnector_factory.h"

namespace p2p_provider {
namespace {

class FakeUserIdProvider : public p2p_provider::UserIdProvider {
 public:
  explicit FakeUserIdProvider(std::string user_id)
      : user_id_(std::move(user_id)) {}

  void GetUserId(std::function<void(Status, std::string)> callback) override {
    callback(Status::OK, user_id_);
  };

 private:
  std::string user_id_;
};

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
        std::make_unique<FakeUserIdProvider>(std::move(user_name)));
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
}  // namespace
}  // namespace p2p_provider
