// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/services/host_name_subscriber_service_impl.h"

#include <fuchsia/net/mdns/cpp/fidl.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace mdns {
namespace test {

class HostNameSubscriberServiceImplTests : public gtest::RealLoopFixture,
                                           public fuchsia::net::mdns::HostNameSubscriptionListener {
 public:
  void OnAddressesChanged(std::vector<fuchsia::net::mdns::HostAddress> addresses,
                          OnAddressesChangedCallback callback) override {
    callback();
  }
};

class TestTransceiver : public Mdns::Transceiver {
 public:
  // Mdns::Transceiver implementation.
  void Start(fuchsia::net::interfaces::WatcherPtr watcher, fit::closure link_change_callback,
             InboundMessageCallback inbound_message_callback,
             InterfaceTransceiverCreateFunction transceiver_factory) override {
    link_change_callback();
  }

  void Stop() override {}

  bool HasInterfaces() override { return true; }

  void SendMessage(const DnsMessage& message, const ReplyAddress& reply_address) override {}

  void LogTraffic() override {}

  std::vector<HostAddress> LocalHostAddresses() override { return std::vector<HostAddress>(); }
};

// Tests that publications outlive subscribers.
TEST_F(HostNameSubscriberServiceImplTests, SubscriptionLifetime) {
  // Instantiate |Mdns| so we can register a subscriber with it.
  TestTransceiver transceiver;
  Mdns mdns(transceiver);
  bool ready_callback_called = false;
  mdns.Start(nullptr, "TestHostName", /* perform probe */ false,
             [&ready_callback_called]() {
               // Ready callback.
               ready_callback_called = true;
             },
             {});

  // Create the subscriber bound to the |subscriber_ptr| channel.
  fuchsia::net::mdns::HostNameSubscriberPtr subscriber_ptr;
  bool delete_callback_called = false;
  auto under_test = std::make_unique<HostNameSubscriberServiceImpl>(
      mdns, subscriber_ptr.NewRequest(), [&delete_callback_called]() {
        // Delete callback.
        delete_callback_called = true;
      });

  // Expect that the |Mdns| instance is ready and the subscriber has not requested deletion.
  RunLoopUntilIdle();
  EXPECT_TRUE(ready_callback_called);
  EXPECT_FALSE(delete_callback_called);

  // Instantiate a subscriber.
  fuchsia::net::mdns::HostNameSubscriptionListenerHandle listener_handle;
  fidl::Binding<fuchsia::net::mdns::HostNameSubscriptionListener> binding(
      this, listener_handle.NewRequest());
  zx_status_t binding_status = ZX_OK;
  binding.set_error_handler([&binding_status](zx_status_t status) { binding_status = status; });

  // Register the subscriber with the |Mdns| instance.
  subscriber_ptr->SubscribeToHostName("TestHostName",
                                      fuchsia::net::mdns::HostNameSubscriptionOptions(),
                                      std::move(listener_handle));

  // Expect the listener binding is fine and the subscriber has not requested deletion.
  RunLoopUntilIdle();
  EXPECT_EQ(ZX_OK, binding_status);
  EXPECT_FALSE(delete_callback_called);

  // Close the subscriber channel. Expect that the listener binding is fine and the subscriber has
  // requested deletion.
  subscriber_ptr = nullptr;
  RunLoopUntilIdle();
  EXPECT_EQ(ZX_OK, binding_status);
  EXPECT_TRUE(delete_callback_called);

  // Actually delete the subscriber as requested by the delete callback. Expect that the binding is
  // fine.
  under_test = nullptr;
  RunLoopUntilIdle();
  EXPECT_EQ(ZX_OK, binding_status);

  binding.Close(ZX_ERR_PEER_CLOSED);
  RunLoopUntilIdle();
}

}  // namespace test
}  // namespace mdns
