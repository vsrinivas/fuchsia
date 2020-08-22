// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/gtest/real_loop_fixture.h"
#include "src/connectivity/network/mdns/service/mdns.h"

namespace mdns {
namespace test {

static const std::string kHostName = "test_host_name";
static const std::string kHostFullName = "test_host_name.local.";
static const std::string kServiceName = "_yardapult._tcp.";
static const std::string kServiceFullName = "_yardapult._tcp.local.";
static const std::string kInstanceName = "my";
static const std::string kInstanceFullName = "my._yardapult._tcp.local.";
static const ReplyAddress kReplyAddress({192, 168, 78, 9, inet::IpPort::From_in_port_t(5353)},
                                        {192, 168, 1, 1}, Media::kWired);

// Unit tests for the |Mdns| class.
class MdnsUnitTests : public gtest::RealLoopFixture, public Mdns::Transceiver {
 public:
  MdnsUnitTests() : under_test_(*this) {}

  //  Mdns::Transceiver implementation.
  void Start(fuchsia::netstack::NetstackPtr netstack, const MdnsAddresses& addresses,
             fit::closure link_change_callback,
             InboundMessageCallback inbound_message_callback) override {
    start_called_ = true;
    link_change_callback_ = std::move(link_change_callback);
    inbound_message_callback_ = std::move(inbound_message_callback);
  }

  void Stop() override { stop_called_ = true; }

  bool HasInterfaces() override { return has_interfaces_; }

  void SendMessage(DnsMessage* message, const ReplyAddress& reply_address) override {
    send_message_called_ = true;
    send_message_reply_address_ = reply_address;
  }

  void LogTraffic() override {}

 protected:
  // The |Mdns| instance under test.
  Mdns& under_test() { return under_test_; }

  // Sets the value returned by |Mdns::Transceiver::HasInterfaces|.
  void SetHasInterfaces(bool has_interfaces) { has_interfaces_ = has_interfaces; }

  // Whether |Mdns::Transceiver::Start| has been called.
  bool start_called() const { return start_called_; }

  // Whether |Mdns::Transceiver::Stop| has been called.
  bool stop_called() const { return stop_called_; }

  // Whether |Mdns::Transceiver::SendMessage| has been called and resets the flag.
  bool get_and_clear_send_message_called() {
    bool result = send_message_called_;
    send_message_called_ = false;
    return result;
  }

  void ExpectSendMessageCalled(const ReplyAddress& reply_address) {
    EXPECT_TRUE(send_message_called_);
    EXPECT_EQ(reply_address, send_message_reply_address_);
    send_message_called_ = false;
  }

  // Whether the ready callback has been called by the unit under test.
  bool ready() const { return ready_; }

  // Starts the |Mdns| instance under test.
  void Start(bool perform_address_probe) {
    under_test_.Start(nullptr, kHostName, addresses_, perform_address_probe,
                      [this]() { ready_ = true; });
    EXPECT_TRUE(start_called_);
    EXPECT_TRUE(link_change_callback_);
    EXPECT_TRUE(inbound_message_callback_);
    EXPECT_FALSE(stop_called_);
    EXPECT_TRUE(perform_address_probe || !has_interfaces_ || ready_);
  }

  // Simulates receipt of a message via the transceiver.
  void ReceiveMessage(std::unique_ptr<DnsMessage> message, const ReplyAddress& reply_address) {
    EXPECT_TRUE(inbound_message_callback_);
    inbound_message_callback_(std::move(message), reply_address);
  }

  // Makes an address resource.
  std::shared_ptr<DnsResource> MakeAddressResource(const std::string& host_full_name,
                                                   const inet::IpAddress& address) {
    std::shared_ptr<DnsResource> resource;

    if (address.is_v4()) {
      resource = std::make_shared<DnsResource>(host_full_name, DnsType::kA);
      resource->a_.address_.address_ = address;
    } else {
      resource = std::make_shared<DnsResource>(host_full_name, DnsType::kAaaa);
      resource->aaaa_.address_.address_ = address;
    }

    return resource;
  }

  // Simulates the receipt of a typical query for PTR resources.
  void ReceiveQuery() {
    auto message = std::make_unique<DnsMessage>();
    auto ptr_question = std::make_shared<DnsQuestion>(kServiceName, DnsType::kPtr);
    message->questions_.push_back(ptr_question);
    message->UpdateCounts();

    ReceiveMessage(std::move(message), kReplyAddress);
  }

  // Simulates the receipt of a typical query response (with PTR, SRC and A resources).
  void ReceiveQueryResponse() {
    auto message = std::make_unique<DnsMessage>();
    auto ptr_resource = std::make_shared<DnsResource>(kServiceFullName, DnsType::kPtr);
    ptr_resource->time_to_live_ = DnsResource::kShortTimeToLive;
    ptr_resource->ptr_.pointer_domain_name_ = kInstanceFullName;
    message->answers_.push_back(ptr_resource);

    auto srv_resource = std::make_shared<DnsResource>(kInstanceFullName, DnsType::kSrv);
    srv_resource->time_to_live_ = DnsResource::kShortTimeToLive;
    srv_resource->srv_.priority_ = 0;
    srv_resource->srv_.weight_ = 0;
    srv_resource->srv_.port_ = inet::IpPort::From_uint16_t(5353);
    srv_resource->srv_.target_ = kHostFullName;
    message->additionals_.push_back(srv_resource);

    message->additionals_.push_back(
        MakeAddressResource(kHostFullName, inet::IpAddress(192, 168, 66, 6)));

    message->header_.SetResponse(true);
    message->header_.SetAuthoritativeAnswer(true);
    message->UpdateCounts();

    ReceiveMessage(std::move(message), kReplyAddress);
  }

 private:
  Mdns under_test_;
  MdnsAddresses addresses_;
  bool has_interfaces_ = false;
  bool start_called_ = false;
  bool stop_called_ = false;
  bool send_message_called_ = false;
  ReplyAddress send_message_reply_address_;
  bool ready_ = false;
  fit::closure link_change_callback_;
  InboundMessageCallback inbound_message_callback_;
};

class Subscriber : public Mdns::Subscriber {
 public:
  Subscriber() {}

  // Mdns::Subscriber implementation.
  void InstanceDiscovered(const std::string& service, const std::string& instance,
                          const inet::SocketAddress& v4_address,
                          const inet::SocketAddress& v6_address,
                          const std::vector<std::string>& text, uint16_t srv_priority,
                          uint16_t srv_weight) override {
    instance_discovered_called_ = true;
  }

  void InstanceChanged(const std::string& service, const std::string& instance,
                       const inet::SocketAddress& v4_address, const inet::SocketAddress& v6_address,
                       const std::vector<std::string>& text, uint16_t srv_priority,
                       uint16_t srv_weight) override {}

  void InstanceLost(const std::string& service, const std::string& instance) override {}

  void Query(DnsType type_queried) override {}

  // Indicates whether the subscriber's 'instance discovered' callback was called, resetting the
  // indication if it was.
  bool InstanceDiscoveredCalled() {
    bool result = instance_discovered_called_;
    instance_discovered_called_ = false;
    return result;
  }

 private:
  bool instance_discovered_called_ = false;
};

// Responds synchronously to |GetPublication| with publication.
class Publisher : public Mdns::Publisher {
 public:
  Publisher() {}

  // Mdns::Publisher implementation.
  void ReportSuccess(bool success) override {}

  void GetPublication(bool query, const std::string& subtype,
                      const std::vector<inet::SocketAddress>& source_addresses,
                      fit::function<void(std::unique_ptr<Mdns::Publication>)> callback) override {
    callback(Mdns::Publication::Create(inet::IpPort::From_in_port_t(5353), {}));
  }
};

// Responds synchronously to |GetPublication| with null.
class NonPublisher : public Mdns::Publisher {
 public:
  NonPublisher() {}

  // Mdns::Publisher implementation.
  void ReportSuccess(bool success) override {}

  void GetPublication(bool query, const std::string& subtype,
                      const std::vector<inet::SocketAddress>& source_addresses,
                      fit::function<void(std::unique_ptr<Mdns::Publication>)> callback) override {
    callback(nullptr);
  }
};

// Responds asynchronously to |GetPublication|.
class AsyncPublisher : public Mdns::Publisher {
 public:
  AsyncPublisher() {}

  // Mdns::Publisher implementation.
  void ReportSuccess(bool success) override {}

  void GetPublication(bool query, const std::string& subtype,
                      const std::vector<inet::SocketAddress>& source_addresses,
                      fit::function<void(std::unique_ptr<Mdns::Publication>)> callback) override {
    get_publication_callback_ = std::move(callback);
  }

  fit::function<void(std::unique_ptr<Mdns::Publication>)> get_publication_callback() {
    return std::move(get_publication_callback_);
  }

 private:
  fit::function<void(std::unique_ptr<Mdns::Publication>)> get_publication_callback_;
};

// Tests a subscription.
TEST_F(MdnsUnitTests, Subscribe) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  Subscriber subscriber;

  // Subscribe.
  under_test().SubscribeToService(kServiceName, &subscriber);
  RunLoopUntilIdle();
  EXPECT_FALSE(subscriber.InstanceDiscoveredCalled());

  // Receive a response to the query.
  ReceiveQueryResponse();
  RunLoopUntilIdle();
  EXPECT_TRUE(subscriber.InstanceDiscoveredCalled());

  // Clean up.
  subscriber.Unsubscribe();
  under_test().Stop();
  RunLoopUntilIdle();
}

// Regression test for fxb/55116.
TEST_F(MdnsUnitTests, Regression55116) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  Subscriber subscriber;

  // Subscribe.
  under_test().SubscribeToService(kServiceName, &subscriber);
  RunLoopUntilIdle();
  EXPECT_FALSE(subscriber.InstanceDiscoveredCalled());

  // Unsubscribe.
  subscriber.Unsubscribe();
  RunLoopUntilIdle();

  // Subscribe again.
  under_test().SubscribeToService(kServiceName, &subscriber);
  RunLoopUntilIdle();
  EXPECT_FALSE(subscriber.InstanceDiscoveredCalled());

  // Receive a response to the query.
  ReceiveQueryResponse();
  EXPECT_TRUE(subscriber.InstanceDiscoveredCalled());

  // Clean up.
  subscriber.Unsubscribe();
  under_test().Stop();
  RunLoopUntilIdle();
}

// Tests publish/unpublish logic.
TEST_F(MdnsUnitTests, PublishUnpublish) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  NonPublisher publisher0;
  NonPublisher publisher1;

  // Publish should work the first time.
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, false, Media::kWired,
                                                  &publisher0));

  // A second attempt should fail.
  EXPECT_FALSE(under_test().PublishServiceInstance(kServiceName, kInstanceName, false,
                                                   Media::kWired, &publisher1));

  // We should be able to unpublish and publish again.
  publisher0.Unpublish();
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, false, Media::kWired,
                                                  &publisher1));

  // Clean up.
  publisher1.Unpublish();
  under_test().Stop();
  RunLoopUntilIdle();
}

// Tests that unpublishing works when an instance prober is running.
TEST_F(MdnsUnitTests, UnpublishDuringProbe) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  NonPublisher publisher;

  // Publish with probe and then immediately unpublish.
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, true, Media::kWired,
                                                  &publisher));
  publisher.Unpublish();
  RunLoopUntilIdle();

  // The prober may send one message immediately due to a random backoff delay that can be zero.
  (void)get_and_clear_send_message_called();

  // The prober sends a message within 250 ms, so wait 300 ms before checking that the prober isn't
  // sending anymore.
  RunLoopWithTimeout(zx::duration(zx::msec(300)));
  EXPECT_FALSE(get_and_clear_send_message_called());

  // Clean up.
  under_test().Stop();
  RunLoopUntilIdle();
}

// Tests async SendMessage functionality.
TEST_F(MdnsUnitTests, AsyncSendMessage) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  AsyncPublisher publisher;

  // Publish should work the first time.
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, false, Media::kWired,
                                                  &publisher));

  // The publisher should get a |GetPublication| call immediately as part of initial announcement.
  auto callback = publisher.get_publication_callback();
  EXPECT_TRUE(callback);
  (void)get_and_clear_send_message_called();

  // We should see |SendMessage| happening immediately after the callback, which ensures that
  // asynchronous callbacks produce immediate results (fxb/58141).
  callback(Mdns::Publication::Create(inet::IpPort::From_in_port_t(5353), {}));
  EXPECT_TRUE(get_and_clear_send_message_called());

  // Clean up.
  publisher.Unpublish();
  under_test().Stop();
  RunLoopUntilIdle();
}

// Tests that a wired-only publisher multicasts to wired interfaces only.
TEST_F(MdnsUnitTests, PublishWiredOnly) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  Publisher publisher;

  // Publish wired-only.
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, false, Media::kWired,
                                                  &publisher));
  RunLoopUntilIdle();
  MdnsAddresses addresses;
  ExpectSendMessageCalled(addresses.multicast_reply_wired_only());

  // Clean up.
  under_test().Stop();
  RunLoopUntilIdle();
}

// Tests that a wireless-only publisher multicasts to wireless interfaces only.
TEST_F(MdnsUnitTests, PublishWirelessOnly) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  Publisher publisher;

  // Publish wired-only.
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, false,
                                                  Media::kWireless, &publisher));
  RunLoopUntilIdle();
  MdnsAddresses addresses;
  ExpectSendMessageCalled(addresses.multicast_reply_wireless_only());

  // Clean up.
  under_test().Stop();
  RunLoopUntilIdle();
}

// Tests that a wireless+wired publisher multicasts to all interfaces.
TEST_F(MdnsUnitTests, PublishBoth) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  Publisher publisher;

  // Publish wired-only.
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, false,
                                                  Media::kBoth, &publisher));
  RunLoopUntilIdle();
  MdnsAddresses addresses;
  ExpectSendMessageCalled(addresses.multicast_reply());

  // Clean up.
  under_test().Stop();
  RunLoopUntilIdle();
}

}  // namespace test
}  // namespace mdns
