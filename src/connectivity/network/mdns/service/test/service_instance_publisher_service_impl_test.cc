// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/services/service_instance_publisher_service_impl.h"

#include <fuchsia/net/mdns/cpp/fidl.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace mdns {
namespace test {

class ServiceInstancePublisherServiceImplTests
    : public gtest::RealLoopFixture,
      public fuchsia::net::mdns::ServiceInstancePublicationResponder {
 public:
  void OnPublication(fuchsia::net::mdns::ServiceInstancePublicationCause publication_cause,
                     fidl::StringPtr subtype, std::vector<fuchsia::net::IpAddress> source_addresses,
                     OnPublicationCallback callback) override {
    callback(fpromise::error(fuchsia::net::mdns::OnPublicationError::DO_NOT_RESPOND));
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

// Tests that publications outlive publishers.
TEST_F(ServiceInstancePublisherServiceImplTests, PublicationLifetime) {
  // Instantiate |Mdns| so we can register a publisher with it.
  TestTransceiver transceiver;
  Mdns mdns(transceiver);
  bool ready_callback_called = false;
  mdns.Start(nullptr, "TestHostName", /* perform probe */ false,
             [&ready_callback_called]() {
               // Ready callback.
               ready_callback_called = true;
             },
             {});

  // Create the publisher bound to the |publisher_ptr| channel.
  fuchsia::net::mdns::ServiceInstancePublisherPtr publisher_ptr;
  bool delete_callback_called = false;
  auto under_test = std::make_unique<ServiceInstancePublisherServiceImpl>(
      mdns, publisher_ptr.NewRequest(), [&delete_callback_called]() {
        // Delete callback.
        delete_callback_called = true;
      });

  // Expect that the |Mdns| instance is ready and the publisher has not requested deletion.
  RunLoopUntilIdle();
  EXPECT_TRUE(ready_callback_called);
  EXPECT_FALSE(delete_callback_called);

  // Instantiate a publisher.
  fuchsia::net::mdns::ServiceInstancePublicationResponderHandle responder_handle;
  fidl::Binding<fuchsia::net::mdns::ServiceInstancePublicationResponder> binding(
      this, responder_handle.NewRequest());
  zx_status_t binding_status = ZX_OK;
  binding.set_error_handler([&binding_status](zx_status_t status) { binding_status = status; });

  // Register the publisher with the |Mdns| instance.
  publisher_ptr->PublishServiceInstance(
      "_testservice._tcp.", "TestInstanceName",
      fuchsia::net::mdns::ServiceInstancePublicationOptions(), std::move(responder_handle),
      [](fuchsia::net::mdns::ServiceInstancePublisher_PublishServiceInstance_Result result) {});

  // Expect the responder binding is fine and the publisher has not requested deletion.
  RunLoopUntilIdle();
  EXPECT_EQ(ZX_OK, binding_status);
  EXPECT_FALSE(delete_callback_called);

  // Close the publisher channel. Expect that the responder binding is fine and the publisher has
  // requested deletion.
  publisher_ptr = nullptr;
  RunLoopUntilIdle();
  EXPECT_EQ(ZX_OK, binding_status);
  EXPECT_TRUE(delete_callback_called);

  // Actually delete the publisher as requested by the delete callback. Expect that the binding is
  // fine.
  under_test = nullptr;
  RunLoopUntilIdle();
  EXPECT_EQ(ZX_OK, binding_status);

  binding.Close(ZX_ERR_PEER_CLOSED);
  RunLoopUntilIdle();
}

}  // namespace test
}  // namespace mdns
