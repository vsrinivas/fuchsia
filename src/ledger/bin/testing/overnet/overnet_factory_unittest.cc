// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/overnet/overnet_factory.h"

#include <fuchsia/overnet/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/fidl_helpers/message_relay.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"

namespace ledger {

namespace {
using ::testing::IsEmpty;

class OvernetFactoryTest : public gtest::TestLoopFixture {
 public:
  OvernetFactoryTest() : factory_(dispatcher()) {}
  OvernetFactoryTest(const OvernetFactoryTest&) = delete;
  OvernetFactoryTest& operator=(const OvernetFactoryTest&) = delete;
  ~OvernetFactoryTest() override = default;

 protected:
  OvernetFactory factory_;
};

// Verifies that the host list is correct for one host with the workaround.
TEST_F(OvernetFactoryTest, HostList_OneHost_Workaround) {
  OvernetFactory factory(dispatcher(), true);
  fuchsia::overnet::OvernetPtr overnet1;
  factory.AddBinding(1u, overnet1.NewRequest());

  bool called = false;
  std::vector<fuchsia::overnet::Peer> host_list;
  overnet1->ListPeers(callback::Capture(callback::SetWhenCalled(&called), &host_list));

  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_THAT(host_list, IsEmpty());

  called = false;
  overnet1->ListPeers(callback::Capture(callback::SetWhenCalled(&called), &host_list));

  RunLoopUntilIdle();
  EXPECT_FALSE(called);
}

// Verifies that the host list is correct for one host.
TEST_F(OvernetFactoryTest, HostList_OneHost) {
  fuchsia::overnet::OvernetPtr overnet1;
  factory_.AddBinding(1u, overnet1.NewRequest());

  bool called = false;
  std::vector<fuchsia::overnet::Peer> host_list;
  overnet1->ListPeers(callback::Capture(callback::SetWhenCalled(&called), &host_list));

  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  ASSERT_EQ(host_list.size(), 1u);
  EXPECT_EQ(host_list.at(0).id.id, 1u);

  called = false;
  overnet1->ListPeers(callback::Capture(callback::SetWhenCalled(&called), &host_list));

  RunLoopUntilIdle();
  EXPECT_FALSE(called);
}

// Verifies that the host list is correct for two hosts.
TEST_F(OvernetFactoryTest, HostList_TwoHosts_Sequence) {
  fuchsia::overnet::OvernetPtr overnet1;
  factory_.AddBinding(1u, overnet1.NewRequest());

  bool called = false;
  std::vector<fuchsia::overnet::Peer> host_list;
  overnet1->ListPeers(callback::Capture(callback::SetWhenCalled(&called), &host_list));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  called = false;
  overnet1->ListPeers(callback::Capture(callback::SetWhenCalled(&called), &host_list));

  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  fuchsia::overnet::OvernetPtr overnet2;
  factory_.AddBinding(2u, overnet2.NewRequest());

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  ASSERT_EQ(host_list.size(), 2u);
  EXPECT_EQ(host_list.at(0).id.id, 1u);
  EXPECT_EQ(host_list.at(1).id.id, 2u);

  called = false;
  overnet2->ListPeers(callback::Capture(callback::SetWhenCalled(&called), &host_list));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  ASSERT_EQ(host_list.size(), 2u);
  EXPECT_EQ(host_list.at(0).id.id, 1u);
  EXPECT_EQ(host_list.at(1).id.id, 2u);

  overnet2.Unbind();
  RunLoopUntilIdle();

  overnet1->ListPeers(callback::Capture(callback::SetWhenCalled(&called), &host_list));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  ASSERT_EQ(host_list.size(), 1u);
  EXPECT_EQ(host_list.at(0).id.id, 1u);
}

// Verifies that the host list is correct for two hosts when calls are chained,
// ie. when we have a pending call for a new host list waiting when a host
// connects or disconnects.
TEST_F(OvernetFactoryTest, HostList_TwoHosts_Chained) {
  fuchsia::overnet::OvernetPtr overnet1;
  factory_.AddBinding(1u, overnet1.NewRequest());

  bool called = false;
  std::vector<fuchsia::overnet::Peer> host_list;
  overnet1->ListPeers(callback::Capture(callback::SetWhenCalled(&called), &host_list));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  called = false;
  overnet1->ListPeers(callback::Capture(callback::SetWhenCalled(&called), &host_list));

  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  fuchsia::overnet::OvernetPtr overnet2;
  factory_.AddBinding(2u, overnet2.NewRequest());

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  ASSERT_EQ(host_list.size(), 2u);
  EXPECT_EQ(host_list.at(0).id.id, 1u);
  EXPECT_EQ(host_list.at(1).id.id, 2u);

  overnet1->ListPeers(callback::Capture(callback::SetWhenCalled(&called), &host_list));

  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  overnet2.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  ASSERT_EQ(host_list.size(), 1u);
  EXPECT_EQ(host_list.at(0).id.id, 1u);
}

class OvernetServiceProvider : public fuchsia::overnet::ServiceProvider {
 public:
  explicit OvernetServiceProvider(std::vector<std::unique_ptr<fidl_helpers::MessageRelay>>* relays)
      : relays_(relays) {}

  void ConnectToService(zx::channel channel,
                        fuchsia::overnet::ConnectionInfo connection_info) override {
    auto relay = std::make_unique<fidl_helpers::MessageRelay>();
    relay->SetChannel(std::move(channel));
    relays_->push_back(std::move(relay));
  }

 private:
  std::vector<std::unique_ptr<fidl_helpers::MessageRelay>>* const relays_;
};

// Tests that two "hosts" can talk to each other through the Overnet
TEST_F(OvernetFactoryTest, ServiceProvider) {
  // Sets up the first host (server).
  fuchsia::overnet::OvernetPtr overnet1;
  factory_.AddBinding(1u, overnet1.NewRequest());

  std::vector<std::unique_ptr<fidl_helpers::MessageRelay>> relays_host1;
  OvernetServiceProvider service_provider(&relays_host1);
  fidl::InterfaceHandle<fuchsia::overnet::ServiceProvider> handle;
  fidl::Binding binding(&service_provider, handle.NewRequest());
  overnet1->PublishService("test_service", std::move(handle));

  RunLoopUntilIdle();

  // Sets up the second host (client).
  fuchsia::overnet::OvernetPtr overnet2;
  factory_.AddBinding(2u, overnet2.NewRequest());
  zx::channel local;
  zx::channel remote;
  zx_status_t status = zx::channel::create(0u, &local, &remote);

  LEDGER_CHECK(status == ZX_OK) << "zx::channel::create failed, status " << status;
  fuchsia::overnet::protocol::NodeId node_id;
  node_id.id = 1u;
  overnet2->ConnectToService(std::move(node_id), "test_service", std::move(remote));

  RunLoopUntilIdle();

  // Verifies that we have received the connection from host2 to host1.
  ASSERT_EQ(relays_host1.size(), 1u);

  // Sets up MessageRelays to abstract sending messages through channels.
  bool called_host1 = false;
  std::vector<uint8_t> message_host1;
  relays_host1[0]->SetMessageReceivedCallback(
      callback::Capture(callback::SetWhenCalled(&called_host1), &message_host1));

  fidl_helpers::MessageRelay relay2;
  relay2.SetChannel(std::move(local));
  bool called_host2 = false;
  std::vector<uint8_t> message_host2;
  relay2.SetMessageReceivedCallback(
      callback::Capture(callback::SetWhenCalled(&called_host2), &message_host2));

  // Sends a message from host2 to host1.
  relay2.SendMessage({0u, 1u});
  RunLoopUntilIdle();

  EXPECT_TRUE(called_host1);
  EXPECT_FALSE(called_host2);
  EXPECT_EQ(message_host1, std::vector<uint8_t>({0u, 1u}));

  // Sends a message from host1 to host2.
  called_host1 = false;
  relays_host1[0]->SendMessage({2u, 3u});
  RunLoopUntilIdle();

  EXPECT_FALSE(called_host1);
  EXPECT_TRUE(called_host2);
  EXPECT_EQ(message_host2, std::vector<uint8_t>({2u, 3u}));

  // Verifies that disconnection works.
  bool relay2_disconnected = false;
  relay2.SetChannelClosedCallback(callback::SetWhenCalled(&relay2_disconnected));
  relays_host1[0].reset();

  RunLoopUntilIdle();
  EXPECT_TRUE(relay2_disconnected);
}
}  // namespace

}  // namespace ledger
