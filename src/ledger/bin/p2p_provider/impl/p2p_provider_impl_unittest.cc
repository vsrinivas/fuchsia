// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_provider/impl/p2p_provider_impl.h"

#include <fuchsia/overnet/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

#include <algorithm>
#include <ostream>
#include <string>

#include "third_party/abseil-cpp/absl/base/attributes.h"

// gtest matchers are in gmock.
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ledger/bin/p2p_provider/impl/make_client_id.h"
#include "src/ledger/bin/p2p_provider/impl/static_user_id_provider.h"
#include "src/ledger/bin/p2p_provider/public/user_id_provider.h"
#include "src/ledger/bin/testing/overnet/overnet_factory.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/convert/convert.h"

namespace p2p_provider {
namespace {

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
    switch (change_type) {
      case DeviceChangeType::NEW:
        device_change_new.push_back(device_name);
        break;
      case DeviceChangeType::DELETED:
        device_change_deleted.push_back(device_name);
        break;
    }
  }

  void OnNewMessage(const P2PClientId& device_name, convert::ExtendedStringView message) override {
    messages.push_back(Message{device_name, convert::ToString(message)});
  }

  ABSL_MUST_USE_RESULT bool HasDevicesChanges(size_t new_device_count,
                                              size_t deleted_device_count) {
    std::cout << device_change_new.size() << " " << device_change_deleted.size() << "\n";
    return device_change_new.size() == new_device_count &&
           device_change_deleted.size() == deleted_device_count;
  }

  std::vector<P2PClientId> device_change_new;
  std::vector<P2PClientId> device_change_deleted;

  std::vector<Message> messages;
};

// This test is templated with a boolean:
// In P2P, different code paths are taken depending on whether their overnet peer ID is greater or
// smaller than an other overnet peer ID.
// In order to test both code paths, the tests are also run with the IDs having their bits flipped.
class P2PProviderImplTest : public ledger::TestWithEnvironment,
                            public ::testing::WithParamInterface<bool> {
 public:
  P2PProviderImplTest() : overnet_factory_(dispatcher()) {}
  P2PProviderImplTest(const P2PProviderImplTest&) = delete;
  P2PProviderImplTest& operator=(const P2PProviderImplTest&) = delete;
  ~P2PProviderImplTest() override = default;

  std::unique_ptr<P2PProvider> GetProvider(uint64_t host_id, std::string user_name) {
    if (GetParam()) {
      host_id = ~host_id;
    }

    fuchsia::overnet::OvernetPtr overnet;
    overnet_factory_.AddBinding(host_id, overnet.NewRequest());
    return std::make_unique<p2p_provider::P2PProviderImpl>(
        std::move(overnet), std::make_unique<StaticUserIdProvider>(std::move(user_name)),
        environment_.random());
  }

 protected:
  void SetUp() override { ::testing::Test::SetUp(); }

  ledger::OvernetFactory overnet_factory_;
};

TEST_P(P2PProviderImplTest, NoSelfPeerNoCrash) {
  std::unique_ptr<P2PProvider> provider1 = GetProvider(1, "user1");
  RecordingClient client1;
  provider1->Start(&client1);
  RunLoopUntilIdle();
  EXPECT_TRUE(client1.HasDevicesChanges(0, 0));
}

TEST_P(P2PProviderImplTest, ThreeHosts_SameUser) {
  std::unique_ptr<P2PProvider> provider1 = GetProvider(1, "user1");
  RecordingClient client1;
  provider1->Start(&client1);
  RunLoopUntilIdle();
  EXPECT_TRUE(client1.HasDevicesChanges(0, 0));

  std::unique_ptr<P2PProvider> provider2 = GetProvider(2, "user1");
  RecordingClient client2;
  provider2->Start(&client2);
  RunLoopUntilIdle();

  EXPECT_TRUE(client1.HasDevicesChanges(1, 0));
  EXPECT_TRUE(client2.HasDevicesChanges(1, 0));

  std::unique_ptr<P2PProvider> provider3 = GetProvider(3, "user1");
  RecordingClient client3;
  provider3->Start(&client3);
  RunLoopUntilIdle();

  EXPECT_TRUE(client1.HasDevicesChanges(2, 0));
  EXPECT_TRUE(client2.HasDevicesChanges(2, 0));
  EXPECT_TRUE(client3.HasDevicesChanges(2, 0));

  // Disconnect one host, and verify disconnection notices are sent.
  provider2.reset();
  RunLoopUntilIdle();

  EXPECT_TRUE(client1.HasDevicesChanges(2, 1));
  EXPECT_TRUE(client2.HasDevicesChanges(2, 0));
  EXPECT_TRUE(client3.HasDevicesChanges(2, 1));
}

TEST_P(P2PProviderImplTest, FourHosts_TwoUsers) {
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

  EXPECT_TRUE(client1.HasDevicesChanges(1, 0));
  EXPECT_TRUE(client2.HasDevicesChanges(1, 0));
  EXPECT_TRUE(client3.HasDevicesChanges(1, 0));
  EXPECT_TRUE(client4.HasDevicesChanges(1, 0));

  provider4.reset();
  RunLoopUntilIdle();

  EXPECT_TRUE(client1.HasDevicesChanges(1, 1));
  EXPECT_TRUE(client2.HasDevicesChanges(1, 0));
  EXPECT_TRUE(client3.HasDevicesChanges(1, 0));
  // |client4| should not be notified of any change because |provider4| was
  // reset.
  EXPECT_TRUE(client4.HasDevicesChanges(1, 0));
}

TEST_P(P2PProviderImplTest, TwoHosts_Messages) {
  std::unique_ptr<P2PProvider> provider1 = GetProvider(1, "user1");
  RecordingClient client1;
  provider1->Start(&client1);
  std::unique_ptr<P2PProvider> provider2 = GetProvider(2, "user1");
  RecordingClient client2;
  provider2->Start(&client2);

  RunLoopUntilIdle();

  EXPECT_TRUE(client1.HasDevicesChanges(1, 0));
  EXPECT_TRUE(client2.HasDevicesChanges(1, 0));
  P2PClientId client1_id_from_pov_of_client2 = client2.device_change_new[0];
  P2PClientId client2_id_from_pov_of_client1 = client1.device_change_new[0];

  // Send message
  EXPECT_TRUE(provider1->SendMessage(client2_id_from_pov_of_client1, "foobar"));
  RunLoopUntilIdle();
  EXPECT_THAT(client1.messages, testing::ElementsAre());
  EXPECT_THAT(client2.messages, testing::ElementsAre(RecordingClient::Message{
                                    client1_id_from_pov_of_client2, "foobar"}));
}

TEST_P(P2PProviderImplTest, TwoHosts_MessagesThenDisconnect) {
  std::unique_ptr<P2PProvider> provider1 = GetProvider(1, "user1");
  RecordingClient client1;
  provider1->Start(&client1);
  std::unique_ptr<P2PProvider> provider2 = GetProvider(2, "user1");
  RecordingClient client2;
  provider2->Start(&client2);

  RunLoopUntilIdle();

  EXPECT_TRUE(client1.HasDevicesChanges(1, 0));
  EXPECT_TRUE(client2.HasDevicesChanges(1, 0));
  P2PClientId client1_id_from_pov_of_client2 = client2.device_change_new[0];
  P2PClientId client2_id_from_pov_of_client1 = client1.device_change_new[0];

  // Send message from client that got its peer from ListPeers
  EXPECT_TRUE(provider1->SendMessage(client2_id_from_pov_of_client1, "foobar"));
  provider1.reset();
  RunLoopUntilIdle();

  EXPECT_THAT(client2.messages, testing::ElementsAre(RecordingClient::Message{
                                    client1_id_from_pov_of_client2, "foobar"}));
}

TEST_P(P2PProviderImplTest, TwoHosts_DisconnectThenReconnect) {
  std::unique_ptr<P2PProvider> provider1 = GetProvider(1, "user1");
  RecordingClient client1;
  provider1->Start(&client1);
  std::unique_ptr<P2PProvider> provider2 = GetProvider(2, "user1");
  RecordingClient client2;
  provider2->Start(&client2);

  RunLoopUntilIdle();

  EXPECT_TRUE(client1.HasDevicesChanges(1, 0));

  provider2.reset();
  RunLoopUntilIdle();

  EXPECT_TRUE(client1.HasDevicesChanges(1, 1));

  std::unique_ptr<P2PProvider> provider2_bis = GetProvider(2, "user1");
  RecordingClient client2_bis;
  provider2_bis->Start(&client2_bis);
  RunLoopUntilIdle();

  EXPECT_TRUE(client1.HasDevicesChanges(2, 1));
}

INSTANTIATE_TEST_SUITE_P(P2PProviderImplTest, P2PProviderImplTest, testing::Values(false, true));

}  // namespace
}  // namespace p2p_provider
