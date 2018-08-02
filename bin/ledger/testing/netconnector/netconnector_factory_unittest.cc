// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/netconnector/netconnector_factory.h"

#include <memory>

#include <fuchsia/netconnector/cpp/fidl.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fxl/macros.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/netconnector/cpp/message_relay.h>

#include "peridot/lib/convert/convert.h"

namespace ledger {

namespace {

class NetConnectorFactoryTest : public gtest::TestLoopFixture {
 public:
  NetConnectorFactoryTest() {}
  ~NetConnectorFactoryTest() override {}

 protected:
  NetConnectorFactory factory_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(NetConnectorFactoryTest);
};

// Verifies that the host list is correct for one host.
TEST_F(NetConnectorFactoryTest, HostList_OneHost) {
  fuchsia::netconnector::NetConnectorPtr netconnector1;
  factory_.AddBinding("host1", netconnector1.NewRequest());

  bool called = false;
  uint64_t version = 0;
  fidl::VectorPtr<fidl::StringPtr> host_list;
  netconnector1->GetKnownDeviceNames(
      fuchsia::netconnector::kInitialKnownDeviceNames,
      callback::Capture(callback::SetWhenCalled(&called), &version,
                        &host_list));

  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_NE(fuchsia::netconnector::kInitialKnownDeviceNames, version);
  ASSERT_GE(1u, host_list->size());
  EXPECT_EQ(1u, host_list->size());
  EXPECT_EQ("host1", host_list->at(0));

  called = false;
  netconnector1->GetKnownDeviceNames(
      version, callback::Capture(callback::SetWhenCalled(&called), &version,
                                 &host_list));

  RunLoopUntilIdle();
  EXPECT_FALSE(called);
}

// Verifies that the host list is correct for two hosts.
TEST_F(NetConnectorFactoryTest, HostList_TwoHosts_Sequence) {
  fuchsia::netconnector::NetConnectorPtr netconnector1;
  factory_.AddBinding("host1", netconnector1.NewRequest());

  bool called = false;
  uint64_t version = 0;
  fidl::VectorPtr<fidl::StringPtr> host_list;
  netconnector1->GetKnownDeviceNames(
      fuchsia::netconnector::kInitialKnownDeviceNames,
      callback::Capture(callback::SetWhenCalled(&called), &version,
                        &host_list));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  called = false;
  uint64_t new_version = version;
  netconnector1->GetKnownDeviceNames(
      version, callback::Capture(callback::SetWhenCalled(&called), &new_version,
                                 &host_list));

  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  fuchsia::netconnector::NetConnectorPtr netconnector2;
  factory_.AddBinding("host2", netconnector2.NewRequest());

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_NE(new_version, version);
  ASSERT_GE(2u, host_list->size());
  EXPECT_EQ(2u, host_list->size());
  EXPECT_EQ("host1", host_list->at(0));
  EXPECT_EQ("host2", host_list->at(1));

  called = false;
  netconnector2->GetKnownDeviceNames(
      fuchsia::netconnector::kInitialKnownDeviceNames,
      callback::Capture(callback::SetWhenCalled(&called), &new_version,
                        &host_list));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  ASSERT_GE(2u, host_list->size());
  EXPECT_EQ(2u, host_list->size());
  EXPECT_EQ("host1", host_list->at(0));
  EXPECT_EQ("host2", host_list->at(1));

  netconnector2.Unbind();

  netconnector1->GetKnownDeviceNames(
      new_version, callback::Capture(callback::SetWhenCalled(&called),
                                     &new_version, &host_list));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  ASSERT_GE(1u, host_list->size());
  EXPECT_EQ(1u, host_list->size());
  EXPECT_EQ("host1", host_list->at(0));
}

// Verifies that the host list is correct for two hosts when calls are chained,
// ie. when we have a pending call for a new host list waiting when a host
// connects or disconnects.
TEST_F(NetConnectorFactoryTest, HostList_TwoHosts_Chained) {
  fuchsia::netconnector::NetConnectorPtr netconnector1;
  factory_.AddBinding("host1", netconnector1.NewRequest());

  bool called = false;
  uint64_t version = 0;
  fidl::VectorPtr<fidl::StringPtr> host_list;
  netconnector1->GetKnownDeviceNames(
      fuchsia::netconnector::kInitialKnownDeviceNames,
      callback::Capture(callback::SetWhenCalled(&called), &version,
                        &host_list));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  called = false;
  uint64_t new_version = version;
  netconnector1->GetKnownDeviceNames(
      version, callback::Capture(callback::SetWhenCalled(&called), &new_version,
                                 &host_list));

  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  fuchsia::netconnector::NetConnectorPtr netconnector2;
  factory_.AddBinding("host2", netconnector2.NewRequest());

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_NE(new_version, version);
  ASSERT_GE(2u, host_list->size());
  EXPECT_EQ(2u, host_list->size());
  EXPECT_EQ("host1", host_list->at(0));
  EXPECT_EQ("host2", host_list->at(1));

  netconnector1->GetKnownDeviceNames(
      new_version, callback::Capture(callback::SetWhenCalled(&called),
                                     &new_version, &host_list));

  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  netconnector2.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  ASSERT_GE(1u, host_list->size());
  EXPECT_EQ(1u, host_list->size());
  EXPECT_EQ("host1", host_list->at(0));
}

TEST_F(NetConnectorFactoryTest, HostList_TwoHosts_Callback) {
  fuchsia::netconnector::NetConnectorPtr netconnector1;
  factory_.AddBinding("host1", netconnector1.NewRequest());

  bool called = false;
  uint64_t version = 0;
  fidl::VectorPtr<fidl::StringPtr> host_list;
  netconnector1->GetKnownDeviceNames(
      fuchsia::netconnector::kInitialKnownDeviceNames,
      callback::Capture(callback::SetWhenCalled(&called), &version,
                        &host_list));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);

  called = false;
  uint64_t new_version;
  netconnector1->GetKnownDeviceNames(
      version, callback::Capture(callback::SetWhenCalled(&called), &new_version,
                                 &host_list));

  RunLoopUntilIdle();
  EXPECT_FALSE(called);

  fuchsia::netconnector::NetConnectorPtr netconnector2;
  factory_.AddBinding("host2", netconnector2.NewRequest());

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_NE(new_version, version);
  ASSERT_GE(2u, host_list->size());
  EXPECT_EQ(2u, host_list->size());
  EXPECT_EQ("host1", host_list->at(0));
  EXPECT_EQ("host2", host_list->at(1));

  bool called2;
  netconnector1->GetKnownDeviceNames(
      new_version, callback::Capture(callback::SetWhenCalled(&called),
                                     &new_version, &host_list));
  netconnector2->GetKnownDeviceNames(
      new_version, callback::Capture(callback::SetWhenCalled(&called2),
                                     &new_version, &host_list));

  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  EXPECT_FALSE(called2);

  netconnector2.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_FALSE(called2);

  ASSERT_GE(1u, host_list->size());
  EXPECT_EQ(1u, host_list->size());
  EXPECT_EQ("host1", host_list->at(0));
}

// Tests that two "hosts" can talk to each other through the NetConnector
TEST_F(NetConnectorFactoryTest, ServiceProvider) {
  // Sets up the first host (server).
  fuchsia::netconnector::NetConnectorPtr netconnector1;
  factory_.AddBinding("host1", netconnector1.NewRequest());

  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> handle;
  component::ServiceProviderImpl service_provider1;
  std::vector<std::unique_ptr<netconnector::MessageRelay>> relays_host1;
  service_provider1.AddBinding(handle.NewRequest());
  service_provider1.AddServiceForName(
      [&relays_host1](zx::channel channel) {
        auto relay = std::make_unique<netconnector::MessageRelay>();
        relay->SetChannel(std::move(channel));
        relays_host1.push_back(std::move(relay));
      },
      "test_service");
  netconnector1->RegisterServiceProvider("test_service", std::move(handle));

  RunLoopUntilIdle();

  // Sets up the second host (client).
  fuchsia::netconnector::NetConnectorPtr netconnector2;
  factory_.AddBinding("host2", netconnector2.NewRequest());
  zx::channel local;
  zx::channel remote;
  zx_status_t status = zx::channel::create(0u, &local, &remote);

  FXL_CHECK(status == ZX_OK) << "zx::channel::create failed, status " << status;

  fuchsia::sys::ServiceProviderPtr service_provider_ptr;
  netconnector2->GetDeviceServiceProvider("host1",
                                          service_provider_ptr.NewRequest());

  service_provider_ptr->ConnectToService("test_service", std::move(remote));

  RunLoopUntilIdle();

  // Verifies that we have received the connection from host2 to host1.
  ASSERT_GE(1u, relays_host1.size());
  EXPECT_EQ(1u, relays_host1.size());

  // Sets up MessageRelays to abstract sending messages through channels.
  bool called_host1 = false;
  std::vector<uint8_t> message_host1;
  relays_host1[0]->SetMessageReceivedCallback(callback::Capture(
      callback::SetWhenCalled(&called_host1), &message_host1));

  netconnector::MessageRelay relay2;
  relay2.SetChannel(std::move(local));
  bool called_host2 = false;
  std::vector<uint8_t> message_host2;
  relay2.SetMessageReceivedCallback(callback::Capture(
      callback::SetWhenCalled(&called_host2), &message_host2));

  // Sends a message from host2 to host1.
  relay2.SendMessage({0u, 1u});
  RunLoopUntilIdle();

  EXPECT_TRUE(called_host1);
  EXPECT_FALSE(called_host2);
  EXPECT_EQ(std::vector<uint8_t>({0u, 1u}), message_host1);

  // Sends a message from host1 to host2.
  called_host1 = false;
  relays_host1[0]->SendMessage({2u, 3u});
  RunLoopUntilIdle();

  EXPECT_FALSE(called_host1);
  EXPECT_TRUE(called_host2);
  EXPECT_EQ(std::vector<uint8_t>({2u, 3u}), message_host2);

  // Verifies that disconnection works.
  bool relay2_disconnected = false;
  relay2.SetChannelClosedCallback(
      callback::SetWhenCalled(&relay2_disconnected));
  relays_host1[0].reset();

  RunLoopUntilIdle();
  EXPECT_TRUE(relay2_disconnected);
}
}  // namespace

}  // namespace ledger
