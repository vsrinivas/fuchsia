// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_provider/impl/p2p_provider_impl.h"

#include <fuchsia/overnet/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>

#include <algorithm>
#include <ostream>
#include <string>

// gtest matchers are in gmock.
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/p2p_provider/impl/make_client_id.h"
#include "src/ledger/bin/p2p_provider/impl/static_user_id_provider.h"
#include "src/ledger/bin/p2p_provider/public/user_id_provider.h"
#include "src/ledger/bin/testing/overnet/overnet_factory.h"
#include "src/lib/fxl/macros.h"

namespace p2p_provider {
namespace {

// Makes a P2PClientId from a raw node identifier. Used for testing.
p2p_provider::P2PClientId MakeP2PClientId(uint64_t id) {
  fuchsia::overnet::protocol::NodeId node_id;
  node_id.id = id;
  return p2p_provider::MakeP2PClientId(std::move(node_id));
}

class RecordingClient : public P2PProvider::Client {
 public:
  struct DeviceChange {
    P2PClientId device;
    DeviceChangeType change;

    bool operator==(const DeviceChange& b) const {
      return device == b.device && change == b.change;
    }
  };

  struct Message {
    P2PClientId source;
    std::string data;

    bool operator==(const Message& b) const { return source == b.source && data == b.data; }
  };

  void OnDeviceChange(const P2PClientId& device_name, DeviceChangeType change_type) override {
    device_changes.push_back(DeviceChange{device_name, change_type});
  }

  void OnNewMessage(const P2PClientId& device_name, fxl::StringView message) override {
    messages.push_back(Message{device_name, message.ToString()});
  }
  std::vector<DeviceChange> device_changes;
  std::vector<Message> messages;
};

std::ostream& operator<<(std::ostream& os, const RecordingClient::DeviceChange& d) {
  return os << "DeviceChange{" << d.device << ", " << bool(d.change) << "}";
}

std::ostream& operator<<(std::ostream& os, const RecordingClient::Message& m) {
  return os << "Message{" << m.source << ", " << m.data << "}";
}

class P2PProviderImplTest : public gtest::TestLoopFixture {
 public:
  P2PProviderImplTest() {}
  ~P2PProviderImplTest() override {}

  std::unique_ptr<P2PProvider> GetProvider(uint64_t host_id, std::string user_name) {
    fuchsia::overnet::OvernetPtr overnet;
    overnet_factory_.AddBinding(host_id, overnet.NewRequest());
    return std::make_unique<p2p_provider::P2PProviderImpl>(
        std::move(overnet), std::make_unique<StaticUserIdProvider>(std::move(user_name)));
  }

 protected:
  void SetUp() override { ::testing::Test::SetUp(); }

  ledger::OvernetFactory overnet_factory_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(P2PProviderImplTest);
};

TEST_F(P2PProviderImplTest, NoSelfPeerNoCrash) {
  std::unique_ptr<P2PProvider> provider1 = GetProvider(1, "user1");
  RecordingClient client1;
  provider1->Start(&client1);
  RunLoopUntilIdle();
  EXPECT_TRUE(client1.device_changes.empty());
}

TEST_F(P2PProviderImplTest, ThreeHosts_SameUser) {
  std::unique_ptr<P2PProvider> provider1 = GetProvider(1, "user1");
  RecordingClient client1;
  provider1->Start(&client1);
  RunLoopUntilIdle();
  EXPECT_TRUE(client1.device_changes.empty());

  std::unique_ptr<P2PProvider> provider2 = GetProvider(2, "user1");
  RecordingClient client2;
  provider2->Start(&client2);
  RunLoopUntilIdle();

  EXPECT_THAT(client1.device_changes, testing::UnorderedElementsAre(RecordingClient::DeviceChange{
                                          MakeP2PClientId(2), DeviceChangeType::NEW}));
  EXPECT_THAT(client2.device_changes, testing::UnorderedElementsAre(RecordingClient::DeviceChange{
                                          MakeP2PClientId(1), DeviceChangeType::NEW}));

  std::unique_ptr<P2PProvider> provider3 = GetProvider(3, "user1");
  RecordingClient client3;
  provider3->Start(&client3);
  RunLoopUntilIdle();

  EXPECT_THAT(client1.device_changes,
              testing::ElementsAre(
                  RecordingClient::DeviceChange{MakeP2PClientId(2), DeviceChangeType::NEW},
                  RecordingClient::DeviceChange{MakeP2PClientId(3), DeviceChangeType::NEW}));

  EXPECT_THAT(client2.device_changes,
              testing::ElementsAre(
                  RecordingClient::DeviceChange{MakeP2PClientId(1), DeviceChangeType::NEW},
                  RecordingClient::DeviceChange{MakeP2PClientId(3), DeviceChangeType::NEW}));

  EXPECT_THAT(client3.device_changes,
              testing::UnorderedElementsAre(
                  RecordingClient::DeviceChange{MakeP2PClientId(1), DeviceChangeType::NEW},
                  RecordingClient::DeviceChange{MakeP2PClientId(2), DeviceChangeType::NEW}));

  // Disconnect one host, and verify disconnection notices are sent.
  provider2.reset();
  RunLoopUntilIdle();

  EXPECT_THAT(client1.device_changes,
              testing::ElementsAre(
                  RecordingClient::DeviceChange{MakeP2PClientId(2), DeviceChangeType::NEW},
                  RecordingClient::DeviceChange{MakeP2PClientId(3), DeviceChangeType::NEW},
                  RecordingClient::DeviceChange{MakeP2PClientId(2), DeviceChangeType::DELETED}));

  EXPECT_THAT(client3.device_changes,
              testing::UnorderedElementsAre(
                  RecordingClient::DeviceChange{MakeP2PClientId(1), DeviceChangeType::NEW},
                  RecordingClient::DeviceChange{MakeP2PClientId(2), DeviceChangeType::NEW},
                  RecordingClient::DeviceChange{MakeP2PClientId(2), DeviceChangeType::DELETED}));
}

TEST_F(P2PProviderImplTest, FourHosts_TwoUsers) {
  std::unique_ptr<P2PProvider> provider1 = GetProvider(1, "user1");
  RecordingClient client1;
  provider1->Start(&client1);
  std::unique_ptr<P2PProvider> provider2 = GetProvider(2, "user2");
  RecordingClient client2;
  provider2->Start(&client2);
  std::unique_ptr<P2PProvider> provider3 = GetProvider(3, "user2");
  RecordingClient client3;
  provider3->Start(&client3);
  std::unique_ptr<P2PProvider> provider4 = GetProvider(4, "user1");
  RecordingClient client4;
  provider4->Start(&client4);
  RunLoopUntilIdle();

  // Verify that only devices with the same user connect together.
  EXPECT_THAT(client1.device_changes, testing::ElementsAre(RecordingClient::DeviceChange{
                                          MakeP2PClientId(4), DeviceChangeType::NEW}));
  EXPECT_THAT(client2.device_changes, testing::ElementsAre(RecordingClient::DeviceChange{
                                          MakeP2PClientId(3), DeviceChangeType::NEW}));
  EXPECT_THAT(client3.device_changes, testing::ElementsAre(RecordingClient::DeviceChange{
                                          MakeP2PClientId(2), DeviceChangeType::NEW}));
  EXPECT_THAT(client4.device_changes, testing::ElementsAre(RecordingClient::DeviceChange{
                                          MakeP2PClientId(1), DeviceChangeType::NEW}));

  provider4.reset();
  RunLoopUntilIdle();

  EXPECT_THAT(client1.device_changes,
              testing::ElementsAre(
                  RecordingClient::DeviceChange{MakeP2PClientId(4), DeviceChangeType::NEW},
                  RecordingClient::DeviceChange{MakeP2PClientId(4), DeviceChangeType::DELETED}));
  EXPECT_THAT(client2.device_changes, testing::ElementsAre(RecordingClient::DeviceChange{
                                          MakeP2PClientId(3), DeviceChangeType::NEW}));
  EXPECT_THAT(client3.device_changes, testing::ElementsAre(RecordingClient::DeviceChange{
                                          MakeP2PClientId(2), DeviceChangeType::NEW}));
}

TEST_F(P2PProviderImplTest, TwoHosts_Messages) {
  std::unique_ptr<P2PProvider> provider1 = GetProvider(1, "user1");
  RecordingClient client1;
  provider1->Start(&client1);
  std::unique_ptr<P2PProvider> provider2 = GetProvider(2, "user1");
  RecordingClient client2;
  provider2->Start(&client2);

  RunLoopUntilIdle();

  EXPECT_TRUE(provider1->SendMessage(MakeP2PClientId(2), "datagram"));
  RunLoopUntilIdle();

  EXPECT_THAT(client1.messages, testing::ElementsAre());
  EXPECT_THAT(client2.messages,
              testing::ElementsAre(RecordingClient::Message{MakeP2PClientId(1), "datagram"}));
}

class MockOvernet : public fuchsia::overnet::Overnet {
 public:
  explicit MockOvernet(fidl::InterfaceRequest<fuchsia::overnet::Overnet> request)
      : binding_(this, std::move(request)) {}

  void RegisterService(
      std::string service_name,
      fidl::InterfaceHandle<fuchsia::overnet::ServiceProvider> service_provider) override {
    FXL_NOTIMPLEMENTED();
  }

  void ConnectToService(fuchsia::overnet::protocol::NodeId node_id, std::string service_name,
                        zx::channel channel) override {
    device_requests.push_back(p2p_provider::MakeP2PClientId(node_id));
  }

  void ListPeers(uint64_t version_last_seen, ListPeersCallback callback) override {
    device_names_callbacks.emplace_back(version_last_seen, std::move(callback));
  }

  std::vector<P2PClientId> device_requests;
  std::vector<std::pair<uint64_t, ListPeersCallback>> device_names_callbacks;

 private:
  fidl::Binding<fuchsia::overnet::Overnet> binding_;
};

// Verifies that P2PProviderImpl does not do symmetrical connections
TEST_F(P2PProviderImplTest, HostConnectionOrdering) {
  fuchsia::overnet::OvernetPtr overnet_ptr_0;
  MockOvernet overnet_impl_0(overnet_ptr_0.NewRequest());
  auto p2p_provider_0 = std::make_unique<p2p_provider::P2PProviderImpl>(
      std::move(overnet_ptr_0), std::make_unique<StaticUserIdProvider>("user"));

  RecordingClient client1;
  p2p_provider_0->Start(&client1);

  RunLoopUntilIdle();

  EXPECT_EQ(overnet_impl_0.device_names_callbacks.size(), 1U);

  auto request1 = std::move(overnet_impl_0.device_names_callbacks[0]);
  fuchsia::overnet::protocol::NodeId node_0;
  node_0.id = 0;
  fuchsia::overnet::protocol::NodeId node_1;
  node_1.id = 1;

  fuchsia::overnet::Peer peer_0_0;
  peer_0_0.id = node_0;
  peer_0_0.is_self = true;
  fuchsia::overnet::Peer peer_0_1;
  peer_0_1.id = node_1;
  peer_0_1.is_self = false;
  std::vector<fuchsia::overnet::Peer> peers_0;
  peers_0.push_back(std::move(peer_0_0));
  peers_0.push_back(std::move(peer_0_1));
  request1.second(1, std::move(peers_0));

  RunLoopUntilIdle();

  fuchsia::overnet::OvernetPtr overnet_ptr_1;
  MockOvernet overnet_impl_1(overnet_ptr_1.NewRequest());
  auto p2p_provider_1 = std::make_unique<p2p_provider::P2PProviderImpl>(
      std::move(overnet_ptr_1), std::make_unique<StaticUserIdProvider>("user"));

  RecordingClient client2;
  p2p_provider_1->Start(&client2);

  RunLoopUntilIdle();

  EXPECT_EQ(overnet_impl_1.device_names_callbacks.size(), 1U);

  auto request2 = std::move(overnet_impl_1.device_names_callbacks[0]);
  fuchsia::overnet::Peer peer_1_0;
  peer_1_0.id = node_0;
  peer_1_0.is_self = false;
  fuchsia::overnet::Peer peer_1_1;
  peer_1_1.id = node_1;
  peer_1_1.is_self = true;
  std::vector<fuchsia::overnet::Peer> peers_1;
  peers_1.push_back(std::move(peer_1_0));
  peers_1.push_back(std::move(peer_1_1));
  request1.second(1, std::move(peers_1));

  RunLoopUntilIdle();

  // Only one device should initiate the connection. We don't really care which
  // one, as long as it is reliably correct.
  EXPECT_EQ(overnet_impl_0.device_requests.size() + overnet_impl_1.device_requests.size(), 1U);
}

}  // namespace
}  // namespace p2p_provider
