// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/host_name_requestor.h"

#include <lib/zx/time.h>

#include <gtest/gtest.h>

#include "src/connectivity/network/mdns/service/test/agent_test.h"

namespace mdns {
namespace test {

class HostNameRequestorTest : public AgentTest {
 public:
  HostNameRequestorTest() = default;

 protected:
  class Subscriber : public Mdns::HostNameSubscriber {
   public:
    void AddressesChanged(std::vector<HostAddress> addresses) override {
      addresses_changed_called_ = true;
      addresses_ = std::move(addresses);
    }

    bool addresses_changed_called() {
      bool result = addresses_changed_called_;
      addresses_changed_called_ = false;
      return result;
    }

    std::vector<HostAddress> addresses() { return std::move(addresses_); }

   private:
    bool addresses_changed_called_ = false;
    std::vector<HostAddress> addresses_;
  };
};

bool VerifyAddresses(std::vector<inet::IpAddress> expected, std::vector<HostAddress> value) {
  for (const auto& host_address : value) {
    auto iter = std::find(expected.begin(), expected.end(), host_address.address());
    if (iter == expected.end()) {
      std::cerr << "Address " << host_address.address() << " was not expected.\n";
      return false;
    }

    expected.erase(iter);
  }

  if (!expected.empty()) {
    std::cerr << expected.size() << " expected addresses not found.\n";
    return false;
  }

  return true;
}

bool VerifyAddresses(std::vector<HostAddress> expected, std::vector<HostAddress> value) {
  for (const auto& host_address : value) {
    auto iter = std::find(expected.begin(), expected.end(), host_address);
    if (iter == expected.end()) {
      std::cerr << "Address " << host_address.address() << " was not expected.\n";
      return false;
    }

    expected.erase(iter);
  }

  if (!expected.empty()) {
    std::cerr << expected.size() << " expected addresses not found.\n";
    return false;
  }

  return true;
}

constexpr char kHostName[] = "testhostname";
constexpr char kHostFullName[] = "testhostname.local.";
const std::vector<inet::IpAddress> kAddresses{inet::IpAddress(192, 168, 1, 200),
                                              inet::IpAddress(0xfe80, 200)};
const std::vector<inet::IpAddress> kOtherAddresses{inet::IpAddress(192, 168, 1, 201),
                                                   inet::IpAddress(0xfe80, 201)};
const std::vector<HostAddress> kHostAddresses{
    HostAddress(inet::IpAddress(192, 168, 1, 200), 1, zx::sec(450)),
    HostAddress(inet::IpAddress(0xfe80, 200), 1, zx::sec(450))};
constexpr uint32_t kInterfaceId = 1;
constexpr bool kIncludeLocal = true;
constexpr bool kExcludeLocal = false;
constexpr bool kIncludeLocalProxies = true;
constexpr bool kExcludeLocalProxies = false;

// Tests nominal startup behavior of the requestor.
TEST_F(HostNameRequestorTest, Nominal) {
  HostNameRequestor under_test(this, kHostName, Media::kBoth, IpVersions::kBoth, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectNoOther();
}

// Tests behavior of the requestor when a host responds to the initial questions.
TEST_F(HostNameRequestorTest, Response) {
  HostNameRequestor under_test(this, kHostName, Media::kBoth, IpVersions::kBoth, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);
  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectNoOther();

  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Respond.
  ReplyAddress sender_address(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address);
  }

  under_test.EndOfMessage();

  // Expect to see the addresses from the response.
  EXPECT_TRUE(subscriber.addresses_changed_called());
  EXPECT_TRUE(VerifyAddresses(kAddresses, subscriber.addresses()));

  for (const auto& address : kAddresses) {
    ExpectRenewCall(DnsResource(kHostFullName, address));
  }

  ExpectNoOther();
}

// Tests behavior of the requestor when a host responds multiple times to the initial questions.
TEST_F(HostNameRequestorTest, Duplicate) {
  HostNameRequestor under_test(this, kHostName, Media::kBoth, IpVersions::kBoth, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);
  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectNoOther();

  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Respond 4 times.
  ReplyAddress sender_address(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  for (size_t i = 0; i < 4; ++i) {
    for (const auto& address : kAddresses) {
      under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                                 sender_address);
    }
  }

  under_test.EndOfMessage();

  // Expect to see the addresses just once. The renewals happen for each response.
  EXPECT_TRUE(subscriber.addresses_changed_called());
  EXPECT_TRUE(VerifyAddresses(kAddresses, subscriber.addresses()));
  for (size_t i = 0; i < 4; ++i) {
    for (const auto& address : kAddresses) {
      ExpectRenewCall(DnsResource(kHostFullName, address));
    }
  }

  ExpectNoOther();

  // Respond again with the same addresses.
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address);
  }

  under_test.EndOfMessage();

  // Expect no new addresses. We still see the renewals.
  EXPECT_FALSE(subscriber.addresses_changed_called());
  for (const auto& address : kAddresses) {
    ExpectRenewCall(DnsResource(kHostFullName, address));
  }

  ExpectNoOther();
}

// Tests behavior of the requestor when the addresses change.
TEST_F(HostNameRequestorTest, Change) {
  HostNameRequestor under_test(this, kHostName, Media::kBoth, IpVersions::kBoth, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);
  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectNoOther();

  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Respond.
  ReplyAddress sender_address(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address);
  }

  under_test.EndOfMessage();

  // Expect to get the addresses.
  EXPECT_TRUE(subscriber.addresses_changed_called());
  EXPECT_TRUE(VerifyAddresses(kAddresses, subscriber.addresses()));
  for (const auto& address : kAddresses) {
    ExpectRenewCall(DnsResource(kHostFullName, address));
  }

  ExpectNoOther();

  // Respond with different addresses.
  for (const auto& address : kOtherAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address);
  }

  under_test.EndOfMessage();

  // Expect to see the new addresses.
  EXPECT_TRUE(subscriber.addresses_changed_called());
  EXPECT_TRUE(VerifyAddresses(kOtherAddresses, subscriber.addresses()));
  for (const auto& address : kOtherAddresses) {
    ExpectRenewCall(DnsResource(kHostFullName, address));
  }

  ExpectNoOther();
}

// Tests behavior of the requestor when host address records expire.
TEST_F(HostNameRequestorTest, Expired) {
  HostNameRequestor under_test(this, kHostName, Media::kBoth, IpVersions::kBoth, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);
  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectNoOther();

  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Respond.
  ReplyAddress sender_address(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address);
  }

  under_test.EndOfMessage();

  // Expect to get the addresses.
  EXPECT_TRUE(subscriber.addresses_changed_called());
  EXPECT_TRUE(VerifyAddresses(kAddresses, subscriber.addresses()));
  for (const auto& address : kAddresses) {
    ExpectRenewCall(DnsResource(kHostFullName, address));
  }

  ExpectNoOther();

  // Send expirations for received response.
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kExpired,
                               sender_address);
  }

  // Expect an empty address list.
  EXPECT_TRUE(subscriber.addresses_changed_called());
  EXPECT_TRUE(subscriber.addresses().empty());
  ExpectNoOther();
}

// Tests the the requestor quits after the last subscriber is removed.
TEST_F(HostNameRequestorTest, Quit) {
  // Need to |make_shared|, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<HostNameRequestor>(
      this, kHostName, Media::kBoth, IpVersions::kBoth, kExcludeLocal, kExcludeLocalProxies);
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectNoOther();

  // Add and remove a subscriber.
  Subscriber subscriber;
  under_test->AddSubscriber(&subscriber);

  under_test->RemoveSubscriber(&subscriber);

  // Expect the requestor to remove itself.
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

// Tests behavior of the requestor when configured for wireless operation only.
TEST_F(HostNameRequestorTest, WirelessOnly) {
  HostNameRequestor under_test(this, kHostName, Media::kWireless, IpVersions::kBoth, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);
  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Expect A and AAAA questions on start.
  auto message =
      ExpectOutboundMessage(ReplyAddress::Multicast(Media::kWireless, IpVersions::kBoth));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectNoOther();

  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Respond on wired.
  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWired, IpVersions::kV4);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address0);
  }

  under_test.EndOfMessage();

  // Expect no addresses.
  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Respond on wireless.
  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address1);
  }

  under_test.EndOfMessage();

  // Expect addresses.
  EXPECT_TRUE(subscriber.addresses_changed_called());
  EXPECT_TRUE(VerifyAddresses(kAddresses, subscriber.addresses()));

  for (const auto& address : kAddresses) {
    ExpectRenewCall(DnsResource(kHostFullName, address));
  }

  ExpectNoOther();
}

// Tests behavior of the requestor when configured for wired operation only.
TEST_F(HostNameRequestorTest, WiredOnly) {
  HostNameRequestor under_test(this, kHostName, Media::kWired, IpVersions::kBoth, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);
  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kWired, IpVersions::kBoth));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectNoOther();

  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Respond on wireless.
  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address0);
  }

  under_test.EndOfMessage();

  // Expect no addresses.
  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Respond on wired.
  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWired, IpVersions::kV4);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address1);
  }

  under_test.EndOfMessage();

  // Expect addresses.
  EXPECT_TRUE(subscriber.addresses_changed_called());
  EXPECT_TRUE(VerifyAddresses(kAddresses, subscriber.addresses()));

  for (const auto& address : kAddresses) {
    ExpectRenewCall(DnsResource(kHostFullName, address));
  }

  ExpectNoOther();
}

// Tests behavior of the requestor when configured for IPv4 operation only.
TEST_F(HostNameRequestorTest, V4Only) {
  HostNameRequestor under_test(this, kHostName, Media::kBoth, IpVersions::kV4, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);
  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kV4));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectNoOther();

  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Respond on IPv6.
  ReplyAddress sender_address0(inet::SocketAddress(0xfe80, 200, inet::IpPort::From_uint16_t(5353)),
                               inet::IpAddress(0xfe80, 100), kInterfaceId, Media::kWireless,
                               IpVersions::kV6);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address0);
  }

  under_test.EndOfMessage();

  // Expect no addresses.
  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Respond on IPv4.
  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWired, IpVersions::kV4);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address1);
  }

  under_test.EndOfMessage();

  // Expect addresses.
  EXPECT_TRUE(subscriber.addresses_changed_called());
  EXPECT_TRUE(VerifyAddresses(kAddresses, subscriber.addresses()));

  for (const auto& address : kAddresses) {
    ExpectRenewCall(DnsResource(kHostFullName, address));
  }

  ExpectNoOther();
}

// Tests behavior of the requestor when configured for IPv6 operation only.
TEST_F(HostNameRequestorTest, V6Only) {
  HostNameRequestor under_test(this, kHostName, Media::kBoth, IpVersions::kV6, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);
  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kV6));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectNoOther();

  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Respond on IPv4.
  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address0);
  }

  under_test.EndOfMessage();

  // Expect no addresses.
  EXPECT_FALSE(subscriber.addresses_changed_called());

  // Respond on IPv6.
  ReplyAddress sender_address1(inet::SocketAddress(0xfe80, 200, inet::IpPort::From_uint16_t(5353)),
                               inet::IpAddress(0xfe80, 100), kInterfaceId, Media::kWired,
                               IpVersions::kV6);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address1);
  }

  under_test.EndOfMessage();

  // Expect addresses.
  EXPECT_TRUE(subscriber.addresses_changed_called());
  EXPECT_TRUE(VerifyAddresses(kAddresses, subscriber.addresses()));

  for (const auto& address : kAddresses) {
    ExpectRenewCall(DnsResource(kHostFullName, address));
  }

  ExpectNoOther();
}

// Tests discovery of the local host.
TEST_F(HostNameRequestorTest, LocalHost) {
  HostNameRequestor under_test(this, kLocalHostName, Media::kBoth, IpVersions::kBoth, kIncludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);
  SetLocalHostAddresses(kHostAddresses);

  under_test.Start(kLocalHostFullName);

  // Expect no questions, because we're requesting the local host.
  ExpectNoOther();

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);
  EXPECT_TRUE(subscriber.addresses_changed_called());
  EXPECT_TRUE(VerifyAddresses(kHostAddresses, subscriber.addresses()));
}

// Tests discovery of a local proxy host.
TEST_F(HostNameRequestorTest, LocalProxyHost) {
  HostNameRequestor under_test(this, kHostName, Media::kBoth, IpVersions::kBoth, kExcludeLocal,
                               kIncludeLocalProxies);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectNoOther();

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);
  EXPECT_FALSE(subscriber.addresses_changed_called());

  under_test.OnAddProxyHost(kHostFullName, kHostAddresses);
  EXPECT_TRUE(subscriber.addresses_changed_called());
  EXPECT_TRUE(VerifyAddresses(kHostAddresses, subscriber.addresses()));

  under_test.OnRemoveProxyHost(kHostFullName);
  EXPECT_TRUE(subscriber.addresses_changed_called());
  EXPECT_TRUE(VerifyAddresses(std::vector<HostAddress>(), subscriber.addresses()));
}

// Tests that a local proxy host is not discovered if that feature is off.
TEST_F(HostNameRequestorTest, NoLocalProxyHost) {
  HostNameRequestor under_test(this, kHostName, Media::kBoth, IpVersions::kBoth, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectNoOther();

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);
  EXPECT_FALSE(subscriber.addresses_changed_called());

  under_test.OnAddProxyHost(kHostFullName, kHostAddresses);
  EXPECT_FALSE(subscriber.addresses_changed_called());
}

}  // namespace test
}  // namespace mdns
