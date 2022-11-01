// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "src/connectivity/network/mdns/service/common/formatters.h"
#include "src/connectivity/network/mdns/service/encoding/dns_formatting.h"
#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace mdns {
namespace test {

constexpr char kHostName[] = "fuchsia-1234-5678-9abc";
constexpr char kLocalHostFullName[] = "fuchsia-1234-5678-9abc.local.";
constexpr char kAltHostFullName[] = "123456789ABC.local.";
constexpr char kServiceName[] = "_yardapult._tcp.";
constexpr char kServiceFullName[] = "_yardapult._tcp.local.";
constexpr char kInstanceName[] = "my";
constexpr char kInstanceFullName[] = "my._yardapult._tcp.local.";
static const ReplyAddress kReplyAddress({192, 168, 78, 9, inet::IpPort::From_uint16_t(5353)},
                                        {192, 168, 1, 1}, 1, Media::kWired, IpVersions::kBoth);
constexpr char kProxyHostName[] = "test_proxy_host_name";
constexpr char kProxyHostFullName[] = "test_proxy_host_name.local.";
const std::vector<inet::IpAddress> kAddresses{inet::IpAddress(192, 168, 1, 200),
                                              inet::IpAddress(192, 168, 1, 201)};
const inet::IpPort kPort = inet::IpPort::From_uint16_t(5353);

// Unit tests for the |Mdns| class.
class MdnsUnitTests : public gtest::RealLoopFixture, public Mdns::Transceiver {
 public:
  MdnsUnitTests() : under_test_(*this) {}

  //  Mdns::Transceiver implementation.
  void Start(fuchsia::net::interfaces::WatcherPtr watcher, fit::closure link_change_callback,
             InboundMessageCallback inbound_message_callback,
             InterfaceTransceiverCreateFunction transceiver_factory) override {
    start_called_ = true;
    link_change_callback_ = std::move(link_change_callback);
    inbound_message_callback_ = std::move(inbound_message_callback);
  }

  void Stop() override { stop_called_ = true; }

  bool HasInterfaces() override { return has_interfaces_; }

  void SendMessage(const DnsMessage& message, const ReplyAddress& reply_address) override {
    send_message_called_ = true;
    send_message_message_ = message;
    send_message_reply_address_ = reply_address;
  }

  void LogTraffic() override {}

  std::vector<HostAddress> LocalHostAddresses() override { return std::vector<HostAddress>(); }

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

  void ExpectSendMessageNotCalled() { EXPECT_FALSE(get_and_clear_send_message_called()); }

  DnsMessage ExpectSendMessageCalled(const ReplyAddress& reply_address) {
    EXPECT_TRUE(send_message_called_);
    EXPECT_EQ(reply_address, send_message_reply_address_);
    send_message_called_ = false;
    return std::move(send_message_message_);
  }

  // Whether the ready callback has been called by the unit under test.
  bool ready() const { return ready_; }

  // Starts the |Mdns| instance under test.
  void Start(bool perform_address_probe, std::vector<std::string> alt_services = {}) {
    under_test_.Start(
        nullptr, kHostName, perform_address_probe, [this]() { ready_ = true; },
        std::move(alt_services));
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

  // Simulates the receipt of a typical query response (with PTR, SRC and A resources).
  void ReceivePtrQueryResponse() {
    auto message = std::make_unique<DnsMessage>();
    auto ptr_resource = std::make_shared<DnsResource>(kServiceFullName, DnsType::kPtr);
    ptr_resource->time_to_live_ = DnsResource::kShortTimeToLive;
    ptr_resource->ptr_.pointer_domain_name_ = DnsName(kInstanceFullName);
    message->answers_.push_back(ptr_resource);

    auto srv_resource = std::make_shared<DnsResource>(kInstanceFullName, DnsType::kSrv);
    srv_resource->time_to_live_ = DnsResource::kShortTimeToLive;
    srv_resource->srv_.priority_ = 0;
    srv_resource->srv_.weight_ = 0;
    srv_resource->srv_.port_ = inet::IpPort::From_uint16_t(5353);
    srv_resource->srv_.target_ = DnsName(kLocalHostFullName);
    message->additionals_.push_back(srv_resource);

    message->additionals_.push_back(
        MakeAddressResource(kLocalHostFullName, inet::IpAddress(192, 168, 66, 6)));

    message->header_.SetResponse(true);
    message->header_.SetAuthoritativeAnswer(true);
    message->UpdateCounts();

    ReceiveMessage(std::move(message), kReplyAddress);
  }

  // Simulates the receipt of a query.
  void ReceiveQuery(const std::string& name, DnsType type, ReplyAddress sender_address) {
    auto message = std::make_unique<DnsMessage>();
    auto ptr_question = std::make_shared<DnsQuestion>(name, type);
    message->questions_.push_back(ptr_question);
    message->UpdateCounts();

    ReceiveMessage(std::move(message), sender_address);
  }

  // Expects that |message| contains a resource in |section| with the given parameters and returns
  // it.
  std::shared_ptr<DnsResource> ExpectResource(DnsMessage& message, MdnsResourceSection section,
                                              const std::string& name, DnsType type,
                                              DnsClass dns_class = DnsClass::kIn,
                                              bool cache_flush = true) {
    auto extracted = ExtractResources(1, message, section, name, type, dns_class, cache_flush);

    EXPECT_FALSE(extracted.empty()) << "No matching resource with name " << name << " and type "
                                    << type << " in section " << section << " of message.";
    return extracted.empty() ? nullptr : std::move(extracted.front());
  }

  // Expects that |message| contains one or more resources in |section| with the given parameters
  // and returns them.
  std::vector<std::shared_ptr<DnsResource>> ExpectResources(DnsMessage& message,
                                                            MdnsResourceSection section,
                                                            const std::string& name, DnsType type,
                                                            DnsClass dns_class = DnsClass::kIn,
                                                            bool cache_flush = true) {
    auto extracted = ExtractResources(std::numeric_limits<size_t>::max(), message, section, name,
                                      type, dns_class, cache_flush);
    EXPECT_FALSE(extracted.empty()) << "No matching resource with name " << name << " and type "
                                    << type << " in section " << section << " of message.";
    return extracted;
  }

  // Expects that |message| contains resources for |addresses| in |section|.
  void ExpectAddresses(DnsMessage& message, MdnsResourceSection section,
                       const std::string& host_full_name,
                       const std::vector<inet::IpAddress>& addresses) {
    bool expect_invalid = false;
    bool expect_v4 = false;
    bool expect_v6 = false;
    for (const auto& address : addresses) {
      if (!address.is_valid()) {
        expect_invalid = true;
      } else if (address.is_v4()) {
        expect_v4 = true;
      } else {
        expect_v6 = true;
      }
    }

    if (expect_invalid) {
      auto resources = ExpectResources(message, section, host_full_name, DnsType::kA);
      for (const auto& address : addresses) {
        if (address.is_valid()) {
          ExpectAddress(resources, address);
        }
      }
    }

    if (expect_v4) {
      auto resources = ExpectResources(message, section, host_full_name, DnsType::kA);
      for (const auto& address : addresses) {
        if (address.is_v4()) {
          ExpectAddress(resources, address);
        }
      }
    }

    if (expect_v6) {
      auto resources = ExpectResources(message, section, host_full_name, DnsType::kAaaa);
      for (const auto& address : addresses) {
        if (address.is_v6()) {
          ExpectAddress(resources, address);
        }
      }
    }
  }

  // Expect that |address| appears in |resources| and remove it.
  void ExpectAddress(std::vector<std::shared_ptr<DnsResource>>& resources,
                     const inet::IpAddress& address) {
    for (auto i = resources.begin(); i != resources.end(); ++i) {
      if ((*i)->a_.address_.address_ == address) {
        resources.erase(i);
        return;
      }
    }

    EXPECT_TRUE(false) << "No matching address " << address;
  }

  // Expects that |message| contains no questions or resources.
  void ExpectNoOtherQuestionOrResource(DnsMessage& message) {
    EXPECT_TRUE(message.questions_.empty());
    EXPECT_TRUE(message.answers_.empty());
    EXPECT_TRUE(message.authorities_.empty());
    EXPECT_TRUE(message.additionals_.empty());
  }

 private:
  // Removes and returns at most |max| resources in |section| with the given parameters.
  std::vector<std::shared_ptr<DnsResource>> ExtractResources(size_t max, DnsMessage& message,
                                                             MdnsResourceSection section,
                                                             const std::string& name, DnsType type,
                                                             DnsClass dns_class = DnsClass::kIn,
                                                             bool cache_flush = true) {
    std::vector<std::shared_ptr<DnsResource>>* collection;
    switch (section) {
      case MdnsResourceSection::kAnswer:
        collection = &message.answers_;
        break;
      case MdnsResourceSection::kAuthority:
        collection = &message.authorities_;
        break;
      case MdnsResourceSection::kAdditional:
        collection = &message.additionals_;
        break;
      case MdnsResourceSection::kExpired:
        EXPECT_TRUE(false);
        return std::vector<std::shared_ptr<DnsResource>>();
    }

    std::vector<std::shared_ptr<DnsResource>> result;
    for (auto i = collection->begin(); i != collection->end();) {
      if ((*i)->name_.dotted_string_ == name && (*i)->type_ == type && (*i)->class_ == dns_class &&
          (*i)->cache_flush_ == cache_flush) {
        result.push_back(std::move(*i));
        i = collection->erase(i);
        if (result.size() == max) {
          return result;
        }
      } else {
        ++i;
      }
    }

    return result;
  }

  Mdns under_test_;
  bool has_interfaces_ = false;
  bool start_called_ = false;
  bool stop_called_ = false;
  bool send_message_called_ = false;
  DnsMessage send_message_message_;
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
                          const std::vector<inet::SocketAddress>& addresses,
                          const std::vector<std::vector<uint8_t>>& text, uint16_t srv_priority,
                          uint16_t srv_weight, const std::string& target) override {
    instance_discovered_called_ = true;
  }

  void InstanceChanged(const std::string& service, const std::string& instance,
                       const std::vector<inet::SocketAddress>& addresses,
                       const std::vector<std::vector<uint8_t>>& text, uint16_t srv_priority,
                       uint16_t srv_weight, const std::string& target) override {}

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

  void GetPublication(PublicationCause publication_cause, const std::string& subtype,
                      const std::vector<inet::SocketAddress>& source_addresses,
                      fit::function<void(std::unique_ptr<Mdns::Publication>)> callback) override {
    callback(Mdns::Publication::Create(kPort, {}));
  }
};

// Responds synchronously to |GetPublication| with null.
class NonPublisher : public Mdns::Publisher {
 public:
  NonPublisher() {}

  // Mdns::Publisher implementation.
  void ReportSuccess(bool success) override {}

  void GetPublication(PublicationCause publication_cause, const std::string& subtype,
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

  void GetPublication(PublicationCause publication_cause, const std::string& subtype,
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

// Responds synchronously to |GetPublication| with publication.
class HostPublisher : public Mdns::HostPublisher {
 public:
  HostPublisher() {}

  // Mdns::HostPublisher implementation.
  void ReportSuccess(bool success) override {}
};

// Tests a subscription.
TEST_F(MdnsUnitTests, Subscribe) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  Subscriber subscriber;

  // Subscribe.
  under_test().SubscribeToService(kServiceName, Media::kBoth, IpVersions::kBoth, false, false,
                                  &subscriber);
  RunLoopUntilIdle();
  EXPECT_FALSE(subscriber.InstanceDiscoveredCalled());

  // Receive a response to the query.
  ReceivePtrQueryResponse();
  RunLoopUntilIdle();
  EXPECT_TRUE(subscriber.InstanceDiscoveredCalled());

  // Clean up.
  subscriber.Unsubscribe();
  under_test().Stop();
  RunLoopUntilIdle();
}

// Regression test for fxbug.dev/55116.
TEST_F(MdnsUnitTests, Regression55116) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  Subscriber subscriber;

  // Subscribe.
  under_test().SubscribeToService(kServiceName, Media::kBoth, IpVersions::kBoth, false, false,
                                  &subscriber);
  RunLoopUntilIdle();
  EXPECT_FALSE(subscriber.InstanceDiscoveredCalled());

  // Unsubscribe.
  subscriber.Unsubscribe();
  RunLoopUntilIdle();

  // Subscribe again.
  under_test().SubscribeToService(kServiceName, Media::kBoth, IpVersions::kBoth, false, false,
                                  &subscriber);
  RunLoopUntilIdle();
  EXPECT_FALSE(subscriber.InstanceDiscoveredCalled());

  // Receive a response to the query.
  ReceivePtrQueryResponse();
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
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, Media::kWired,
                                                  IpVersions::kBoth, /*perform_probe*/ false,
                                                  &publisher0));

  // A second attempt should fail.
  EXPECT_FALSE(under_test().PublishServiceInstance(kServiceName, kInstanceName, Media::kWired,
                                                   IpVersions::kBoth, /*perform_probe*/ false,
                                                   &publisher1));

  // We should be able to unpublish and publish again.
  publisher0.Unpublish();
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, Media::kWired,
                                                  IpVersions::kBoth, /*perform_probe*/ false,
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
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, Media::kWired,
                                                  IpVersions::kBoth, true, &publisher));
  publisher.Unpublish();
  RunLoopUntilIdle();

  // The prober may send one message immediately due to a random backoff delay that can be zero.
  (void)get_and_clear_send_message_called();

  // The prober sends a message within 250 ms, so wait 300 ms before checking that the prober isn't
  // sending anymore.
  RunLoopWithTimeout(zx::duration(zx::msec(300)));
  ExpectSendMessageNotCalled();

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
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, Media::kWired,
                                                  IpVersions::kBoth, false, &publisher));

  // The publisher should get a |GetPublication| call immediately as part of initial announcement.
  auto callback = publisher.get_publication_callback();
  EXPECT_TRUE(callback);
  (void)get_and_clear_send_message_called();

  // We should see |SendMessage| happening immediately after the callback, which ensures that
  // asynchronous callbacks produce immediate results (fxbug.dev/58141).
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
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, Media::kWired,
                                                  IpVersions::kBoth, false, &publisher));
  RunLoopUntilIdle();
  ExpectSendMessageCalled(ReplyAddress::Multicast(Media::kWired, IpVersions::kBoth));

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
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, Media::kWireless,
                                                  IpVersions::kBoth, false, &publisher));
  RunLoopUntilIdle();
  ExpectSendMessageCalled(ReplyAddress::Multicast(Media::kWireless, IpVersions::kBoth));

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
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, Media::kBoth,
                                                  IpVersions::kBoth, false, &publisher));
  RunLoopUntilIdle();
  ExpectSendMessageCalled(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));

  // Clean up.
  under_test().Stop();
  RunLoopUntilIdle();
}

// Tests that a IPv4-only publisher multicasts to IPv4 interfaces only.
TEST_F(MdnsUnitTests, PublishV4Only) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  Publisher publisher;

  // Publish wired-only.
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, Media::kBoth,
                                                  IpVersions::kV4, false, &publisher));
  RunLoopUntilIdle();
  ExpectSendMessageCalled(ReplyAddress::Multicast(Media::kBoth, IpVersions::kV4));

  // Clean up.
  under_test().Stop();
  RunLoopUntilIdle();
}

// Tests that a IPv6-only publisher multicasts to IPv6 interfaces only.
TEST_F(MdnsUnitTests, PublishV6Only) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  Publisher publisher;

  // Publish wired-only.
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, Media::kBoth,
                                                  IpVersions::kV6, false, &publisher));
  RunLoopUntilIdle();
  ExpectSendMessageCalled(ReplyAddress::Multicast(Media::kBoth, IpVersions::kV6));

  // Clean up.
  under_test().Stop();
  RunLoopUntilIdle();
}

// Tests that a publisher with non-default host name and addresses behaves properly.
TEST_F(MdnsUnitTests, PublishInstanceWithHostNameAndAddresses) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  Publisher publisher;

  EXPECT_TRUE(under_test().PublishServiceInstance(kProxyHostName, kAddresses, kServiceName,
                                                  kInstanceName, Media::kBoth, IpVersions::kBoth,
                                                  false, &publisher));
  RunLoopUntilIdle();
  auto message = ExpectSendMessageCalled(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));

  auto resource = ExpectResource(message, MdnsResourceSection::kAnswer, kServiceFullName,
                                 DnsType::kPtr, DnsClass::kIn, false);
  EXPECT_EQ(kInstanceFullName, resource->ptr_.pointer_domain_name_.dotted_string_);

  resource =
      ExpectResource(message, MdnsResourceSection::kAdditional, kInstanceFullName, DnsType::kSrv);
  EXPECT_EQ(0, resource->srv_.priority_);
  EXPECT_EQ(0, resource->srv_.weight_);
  EXPECT_EQ(kPort, resource->srv_.port_);
  EXPECT_EQ(kProxyHostFullName, resource->srv_.target_.dotted_string_);

  resource =
      ExpectResource(message, MdnsResourceSection::kAdditional, kInstanceFullName, DnsType::kTxt);
  EXPECT_TRUE(resource->txt_.strings_.empty());

  ExpectAddresses(message, MdnsResourceSection::kAdditional, kProxyHostFullName, kAddresses);

  ExpectNoOtherQuestionOrResource(message);

  // Clean up.
  under_test().Stop();
  RunLoopUntilIdle();
}

// Tests |PublishHost|.
TEST_F(MdnsUnitTests, PublishHost) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  HostPublisher host_publisher;

  EXPECT_TRUE(under_test().PublishHost(kProxyHostName, kAddresses, Media::kBoth, IpVersions::kBoth,
                                       false, &host_publisher));
  RunLoopUntilIdle();
  ExpectSendMessageNotCalled();

  ReceiveQuery(kProxyHostFullName, DnsType::kA, kReplyAddress);
  RunLoopUntilIdle();
  auto message = ExpectSendMessageCalled(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectAddresses(message, MdnsResourceSection::kAnswer, kProxyHostFullName, kAddresses);

  // Clean up.
  under_test().Stop();
  RunLoopUntilIdle();
}

// Tests |PublishHost| responding on wired-only.
TEST_F(MdnsUnitTests, PublishHostWiredOnly) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  HostPublisher host_publisher;

  EXPECT_TRUE(under_test().PublishHost(kProxyHostName, kAddresses, Media::kWired, IpVersions::kBoth,
                                       false, &host_publisher));
  RunLoopUntilIdle();
  ExpectSendMessageNotCalled();

  // Expect no response to wireless query.
  ReplyAddress sender_address0({192, 168, 78, 9, inet::IpPort::From_uint16_t(5353)},
                               {192, 168, 1, 1}, 1, Media::kWireless, IpVersions::kBoth);
  ReceiveQuery(kProxyHostFullName, DnsType::kA, sender_address0);
  RunLoopUntilIdle();
  ExpectSendMessageNotCalled();

  // Expect response to wired query.
  ReplyAddress sender_address1({192, 168, 78, 9, inet::IpPort::From_uint16_t(5353)},
                               {192, 168, 1, 1}, 1, Media::kWired, IpVersions::kBoth);
  ReceiveQuery(kProxyHostFullName, DnsType::kA, sender_address1);
  RunLoopUntilIdle();

  auto message = ExpectSendMessageCalled(ReplyAddress::Multicast(Media::kWired, IpVersions::kBoth));
  ExpectAddresses(message, MdnsResourceSection::kAnswer, kProxyHostFullName, kAddresses);

  // Clean up.
  under_test().Stop();
  RunLoopUntilIdle();
}

// Tests |PublishHost| responding on wireless-only.
TEST_F(MdnsUnitTests, PublishHostWirelessOnly) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  HostPublisher host_publisher;

  EXPECT_TRUE(under_test().PublishHost(kProxyHostName, kAddresses, Media::kWireless,
                                       IpVersions::kBoth, false, &host_publisher));
  RunLoopUntilIdle();
  ExpectSendMessageNotCalled();

  // Expect no response to wired query.
  ReplyAddress sender_address0({192, 168, 78, 9, inet::IpPort::From_uint16_t(5353)},
                               {192, 168, 1, 1}, 1, Media::kWired, IpVersions::kBoth);
  ReceiveQuery(kProxyHostFullName, DnsType::kA, sender_address0);
  RunLoopUntilIdle();
  ExpectSendMessageNotCalled();

  // Expect response to wireless query.
  ReplyAddress sender_address1({192, 168, 78, 9, inet::IpPort::From_uint16_t(5353)},
                               {192, 168, 1, 1}, 1, Media::kWireless, IpVersions::kBoth);
  ReceiveQuery(kProxyHostFullName, DnsType::kA, sender_address1);
  RunLoopUntilIdle();

  auto message =
      ExpectSendMessageCalled(ReplyAddress::Multicast(Media::kWireless, IpVersions::kBoth));
  ExpectAddresses(message, MdnsResourceSection::kAnswer, kProxyHostFullName, kAddresses);

  // Clean up.
  under_test().Stop();
  RunLoopUntilIdle();
}

// Tests |PublishHost| responding on IPv4 only.
TEST_F(MdnsUnitTests, PublishHostV4Only) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  HostPublisher host_publisher;

  EXPECT_TRUE(under_test().PublishHost(kProxyHostName, kAddresses, Media::kBoth, IpVersions::kV4,
                                       false, &host_publisher));
  RunLoopUntilIdle();
  ExpectSendMessageNotCalled();

  // Expect no response to an IPv6 query.
  ReplyAddress sender_address0({0xfe80, 9, inet::IpPort::From_uint16_t(5353)}, {0xfe80, 1}, 1,
                               Media::kBoth, IpVersions::kV6);
  ReceiveQuery(kProxyHostFullName, DnsType::kAaaa, sender_address0);
  RunLoopUntilIdle();
  ExpectSendMessageNotCalled();

  // Expect response to an IPv4 query.
  ReplyAddress sender_address1({192, 168, 78, 9, inet::IpPort::From_uint16_t(5353)},
                               {192, 168, 1, 1}, 1, Media::kBoth, IpVersions::kV4);
  ReceiveQuery(kProxyHostFullName, DnsType::kA, sender_address1);
  RunLoopUntilIdle();

  auto message = ExpectSendMessageCalled(ReplyAddress::Multicast(Media::kBoth, IpVersions::kV4));
  ExpectAddresses(message, MdnsResourceSection::kAnswer, kProxyHostFullName, kAddresses);

  // Clean up.
  under_test().Stop();
  RunLoopUntilIdle();
}

// Tests |PublishHost| responding on IPv6 only.
TEST_F(MdnsUnitTests, PublishHostV6Only) {
  // Start.
  SetHasInterfaces(true);
  Start(false);

  HostPublisher host_publisher;

  EXPECT_TRUE(under_test().PublishHost(kProxyHostName, kAddresses, Media::kBoth, IpVersions::kV6,
                                       false, &host_publisher));
  RunLoopUntilIdle();
  ExpectSendMessageNotCalled();

  // Expect no response to an IPv4 query.
  ReplyAddress sender_address0({192, 168, 78, 9, inet::IpPort::From_uint16_t(5353)},
                               {192, 168, 1, 1}, 1, Media::kBoth, IpVersions::kV4);
  ReceiveQuery(kProxyHostFullName, DnsType::kA, sender_address0);
  RunLoopUntilIdle();
  ExpectSendMessageNotCalled();

  // Expect response to an IPv6 query.
  ReplyAddress sender_address1({0xfe80, 9, inet::IpPort::From_uint16_t(5353)}, {0xfe80, 1}, 1,
                               Media::kBoth, IpVersions::kV6);
  ReceiveQuery(kProxyHostFullName, DnsType::kAaaa, sender_address1);
  RunLoopUntilIdle();

  auto message = ExpectSendMessageCalled(ReplyAddress::Multicast(Media::kBoth, IpVersions::kV6));
  ExpectAddresses(message, MdnsResourceSection::kAnswer, kProxyHostFullName, kAddresses);

  // Clean up.
  under_test().Stop();
  RunLoopUntilIdle();
}

// Tests that an 'alternate' publisher publishes from the alternate host name.
TEST_F(MdnsUnitTests, PublishAlt) {
  // Start.
  SetHasInterfaces(true);
  Start(false, {kServiceName});

  Publisher publisher;

  // Publish alt service.
  EXPECT_TRUE(under_test().PublishServiceInstance(kServiceName, kInstanceName, Media::kBoth,
                                                  IpVersions::kBoth, false, &publisher));
  RunLoopUntilIdle();
  auto message = ExpectSendMessageCalled(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectAddresses(message, MdnsResourceSection::kAdditional, kAltHostFullName, {inet::IpAddress()});

  // Clean up.
  under_test().Stop();
  RunLoopUntilIdle();
}

// Tests that the alternate host name has a responder when alt services are specified.
TEST_F(MdnsUnitTests, RespondAlt) {
  // Start.
  SetHasInterfaces(true);
  Start(false, {kServiceName});

  RunLoopUntilIdle();
  ExpectSendMessageNotCalled();

  ReceiveQuery(kAltHostFullName, DnsType::kA, kReplyAddress);
  RunLoopUntilIdle();
  auto message = ExpectSendMessageCalled(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectAddresses(message, MdnsResourceSection::kAnswer, kAltHostFullName, {inet::IpAddress()});

  // Clean up.
  under_test().Stop();
  RunLoopUntilIdle();
}

}  // namespace test
}  // namespace mdns
