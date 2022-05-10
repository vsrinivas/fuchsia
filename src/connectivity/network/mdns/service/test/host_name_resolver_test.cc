// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/host_name_resolver.h"

#include <lib/zx/time.h>

#include <gtest/gtest.h>

#include "src/connectivity/network/mdns/service/test/agent_test.h"

namespace mdns {
namespace test {

class HostNameResolverTest : public AgentTest {
 public:
  HostNameResolverTest() = default;
};

bool VerifyAddresses(std::vector<inet::IpAddress> expected, std::vector<HostAddress> value);

constexpr char kHostName[] = "testhostname";
constexpr char kHostFullName[] = "testhostname.local.";
const std::vector<inet::IpAddress> kAddresses{inet::IpAddress(192, 168, 1, 200),
                                              inet::IpAddress(0xfe80, 200)};
const zx::duration timeout = zx::sec(3);
constexpr uint32_t kInterfaceId = 1;

// Tests nominal startup behavior of the resolver.
TEST_F(HostNameResolverTest, Nominal) {
  HostNameResolver under_test(
      this, kHostName, Media::kBoth, IpVersions::kBoth, timeout,
      [](const std::string& host_name, std::vector<HostAddress> addresses) {});
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  // Expect a timer to be set for the timeout.
  ExpectPostTaskForTime(timeout, timeout);
  ExpectNoOther();
}

// Tests behavior of the resolver when a host responds to the initial questions.
TEST_F(HostNameResolverTest, Response) {
  bool callback_called = false;
  std::string callback_host_name;
  std::vector<HostAddress> callback_addresses;
  HostNameResolver under_test(
      this, kHostName, Media::kBoth, IpVersions::kBoth, timeout,
      [&callback_called, &callback_host_name, &callback_addresses](
          const std::string& host_name, std::vector<HostAddress> addresses) {
        callback_called = true;
        callback_host_name = host_name;
        callback_addresses = std::move(addresses);
      });
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  // Expect a timer to be set for the timeout.
  ExpectPostTaskForTime(timeout, timeout);
  ExpectNoOther();

  EXPECT_FALSE(callback_called);

  // Respond.
  ReplyAddress sender_address(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address);
  }

  under_test.EndOfMessage();

  // Expect the callback has been called.
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(kHostName, callback_host_name);
  EXPECT_TRUE(VerifyAddresses(kAddresses, callback_addresses));

  // Expect the resolver to schedule its own removal.
  ExpectPostTaskForTime(zx::sec(0), zx::sec(0));

  ExpectNoOther();
}

// Tests behavior of the resolver when no host responds and the resolver times out.
TEST_F(HostNameResolverTest, Timeout) {
  bool callback_called = false;
  std::string callback_host_name;
  std::vector<HostAddress> callback_addresses;
  // Need to |make_shared|, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<HostNameResolver>(
      this, kHostName, Media::kBoth, IpVersions::kBoth, timeout,
      [&callback_called, &callback_host_name, &callback_addresses](
          const std::string& host_name, std::vector<HostAddress> addresses) {
        callback_called = true;
        callback_host_name = host_name;
        callback_addresses = std::move(addresses);
      });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());

  EXPECT_FALSE(callback_called);

  // Expect a timer to be set for the timeout. Update the time and invoke the task.
  ExpectPostTaskForTimeAndInvoke(timeout, timeout);

  // Expect the callback has been called with no addresses.
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(kHostName, callback_host_name);
  EXPECT_TRUE(callback_addresses.empty());

  // Expect that the agent has removed itself.
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

// Tests behavior of the resolver when configured for wireless only.
TEST_F(HostNameResolverTest, WirelessOnly) {
  bool callback_called = false;
  std::string callback_host_name;
  std::vector<HostAddress> callback_addresses;
  HostNameResolver under_test(
      this, kHostName, Media::kWireless, IpVersions::kBoth, timeout,
      [&callback_called, &callback_host_name, &callback_addresses](
          const std::string& host_name, std::vector<HostAddress> addresses) {
        callback_called = true;
        callback_host_name = host_name;
        callback_addresses = std::move(addresses);
      });
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  // Expect A and AAAA questions on start.
  auto message =
      ExpectOutboundMessage(ReplyAddress::Multicast(Media::kWireless, IpVersions::kBoth));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  // Expect a timer to be set for the timeout.
  ExpectPostTaskForTime(timeout, timeout);
  ExpectNoOther();

  EXPECT_FALSE(callback_called);

  // Respond on wired.
  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWired, IpVersions::kV4);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address0);
  }

  under_test.EndOfMessage();

  // Expect that the callback has not been called
  EXPECT_FALSE(callback_called);

  // Respond on wire;ess.
  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address1);
  }

  under_test.EndOfMessage();

  // Expect the callback has been called.
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(kHostName, callback_host_name);
  EXPECT_TRUE(VerifyAddresses(kAddresses, callback_addresses));

  // Expect the resolver to schedule its own removal.
  ExpectPostTaskForTime(zx::sec(0), zx::sec(0));

  ExpectNoOther();
}

// Tests behavior of the resolver when configured for wired only.
TEST_F(HostNameResolverTest, WiredOnly) {
  bool callback_called = false;
  std::string callback_host_name;
  std::vector<HostAddress> callback_addresses;
  HostNameResolver under_test(
      this, kHostName, Media::kWired, IpVersions::kBoth, timeout,
      [&callback_called, &callback_host_name, &callback_addresses](
          const std::string& host_name, std::vector<HostAddress> addresses) {
        callback_called = true;
        callback_host_name = host_name;
        callback_addresses = std::move(addresses);
      });
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kWired, IpVersions::kBoth));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  // Expect a timer to be set for the timeout.
  ExpectPostTaskForTime(timeout, timeout);
  ExpectNoOther();

  EXPECT_FALSE(callback_called);

  // Respond on wireless.
  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address0);
  }

  under_test.EndOfMessage();

  // Expect that the callback has not been called
  EXPECT_FALSE(callback_called);

  // Respond on wired.
  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWired, IpVersions::kV4);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address1);
  }

  under_test.EndOfMessage();

  // Expect the callback has been called.
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(kHostName, callback_host_name);
  EXPECT_TRUE(VerifyAddresses(kAddresses, callback_addresses));

  // Expect the resolver to schedule its own removal.
  ExpectPostTaskForTime(zx::sec(0), zx::sec(0));

  ExpectNoOther();
}

// Tests behavior of the resolver when configured for IPv4 only.
TEST_F(HostNameResolverTest, V4Only) {
  bool callback_called = false;
  std::string callback_host_name;
  std::vector<HostAddress> callback_addresses;
  HostNameResolver under_test(
      this, kHostName, Media::kBoth, IpVersions::kV4, timeout,
      [&callback_called, &callback_host_name, &callback_addresses](
          const std::string& host_name, std::vector<HostAddress> addresses) {
        callback_called = true;
        callback_host_name = host_name;
        callback_addresses = std::move(addresses);
      });
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kV4));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  // Expect a timer to be set for the timeout.
  ExpectPostTaskForTime(timeout, timeout);
  ExpectNoOther();

  EXPECT_FALSE(callback_called);

  // Respond on IPv6.
  ReplyAddress sender_address0(inet::SocketAddress(0xfe80, 200, inet::IpPort::From_uint16_t(5353)),
                               inet::IpAddress(0xfe80, 100), kInterfaceId, Media::kWireless,
                               IpVersions::kV6);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address0);
  }

  under_test.EndOfMessage();

  // Expect that the callback has not been called
  EXPECT_FALSE(callback_called);

  // Respond on IPv4.
  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address1);
  }

  under_test.EndOfMessage();

  // Expect the callback has been called.
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(kHostName, callback_host_name);
  EXPECT_TRUE(VerifyAddresses(kAddresses, callback_addresses));

  // Expect the resolver to schedule its own removal.
  ExpectPostTaskForTime(zx::sec(0), zx::sec(0));

  ExpectNoOther();
}

// Tests behavior of the resolver when configured for IPv6 only.
TEST_F(HostNameResolverTest, V6Only) {
  bool callback_called = false;
  std::string callback_host_name;
  std::vector<HostAddress> callback_addresses;
  HostNameResolver under_test(
      this, kHostName, Media::kBoth, IpVersions::kV6, timeout,
      [&callback_called, &callback_host_name, &callback_addresses](
          const std::string& host_name, std::vector<HostAddress> addresses) {
        callback_called = true;
        callback_host_name = host_name;
        callback_addresses = std::move(addresses);
      });
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  // Expect A and AAAA questions on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kV6));
  ExpectQuestion(message.get(), kHostFullName, DnsType::kA);
  ExpectQuestion(message.get(), kHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());
  // Expect a timer to be set for the timeout.
  ExpectPostTaskForTime(timeout, timeout);
  ExpectNoOther();

  EXPECT_FALSE(callback_called);

  // Respond on IPv4.
  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 200, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address0);
  }

  under_test.EndOfMessage();

  // Expect that the callback has not been called
  EXPECT_FALSE(callback_called);

  // Respond on IPv6.
  ReplyAddress sender_address1(inet::SocketAddress(0xfe80, 200, inet::IpPort::From_uint16_t(5353)),
                               inet::IpAddress(0xfe80, 100), kInterfaceId, Media::kWireless,
                               IpVersions::kV6);
  for (const auto& address : kAddresses) {
    under_test.ReceiveResource(DnsResource(kHostFullName, address), MdnsResourceSection::kAnswer,
                               sender_address1);
  }

  under_test.EndOfMessage();

  // Expect the callback has been called.
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(kHostName, callback_host_name);
  EXPECT_TRUE(VerifyAddresses(kAddresses, callback_addresses));

  // Expect the resolver to schedule its own removal.
  ExpectPostTaskForTime(zx::sec(0), zx::sec(0));

  ExpectNoOther();
}

}  // namespace test
}  // namespace mdns
