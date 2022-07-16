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
bool VerifyAddresses(std::vector<HostAddress> expected, std::vector<HostAddress> value);

constexpr char kHostName[] = "testhostname";
constexpr char kHostFullName[] = "testhostname.local.";
const std::vector<inet::IpAddress> kAddresses{inet::IpAddress(192, 168, 1, 200),
                                              inet::IpAddress(0xfe80, 200)};
const std::vector<HostAddress> kHostAddresses{
    HostAddress(inet::IpAddress(192, 168, 1, 200), 1, zx::sec(450)),
    HostAddress(inet::IpAddress(0xfe80, 200), 1, zx::sec(450))};
const zx::duration timeout = zx::sec(3);
constexpr uint32_t kInterfaceId = 1;
constexpr bool kIncludeLocal = true;
constexpr bool kExcludeLocal = false;
constexpr bool kIncludeLocalProxies = true;
constexpr bool kExcludeLocalProxies = false;

// Tests nominal startup behavior of the resolver.
TEST_F(HostNameResolverTest, Nominal) {
  HostNameResolver under_test(
      this, kHostName, Media::kBoth, IpVersions::kBoth, kExcludeLocal, kExcludeLocalProxies,
      timeout, [](const std::string& host_name, std::vector<HostAddress> addresses) {});
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
      this, kHostName, Media::kBoth, IpVersions::kBoth, kExcludeLocal, kExcludeLocalProxies,
      timeout,
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
      this, kHostName, Media::kBoth, IpVersions::kBoth, kExcludeLocal, kExcludeLocalProxies,
      timeout,
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
  ExpectPostTaskForTimeAndInvoke(zx::sec(0), zx::sec(0));
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

// Tests behavior of the resolver when configured for wireless only.
TEST_F(HostNameResolverTest, WirelessOnly) {
  bool callback_called = false;
  std::string callback_host_name;
  std::vector<HostAddress> callback_addresses;
  HostNameResolver under_test(
      this, kHostName, Media::kWireless, IpVersions::kBoth, kExcludeLocal, kExcludeLocalProxies,
      timeout,
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
      this, kHostName, Media::kWired, IpVersions::kBoth, kExcludeLocal, kExcludeLocalProxies,
      timeout,
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
      this, kHostName, Media::kBoth, IpVersions::kV4, kExcludeLocal, kExcludeLocalProxies, timeout,
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
      this, kHostName, Media::kBoth, IpVersions::kV6, kExcludeLocal, kExcludeLocalProxies, timeout,
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

// Tests resolution of the local host.
TEST_F(HostNameResolverTest, LocalHost) {
  bool callback_called = false;
  std::string callback_host_name;
  std::vector<HostAddress> callback_addresses;
  HostNameResolver under_test(
      this, kLocalHostName, Media::kBoth, IpVersions::kBoth, kIncludeLocal, kExcludeLocalProxies,
      timeout,
      [&callback_called, &callback_host_name, &callback_addresses](
          const std::string& host_name, std::vector<HostAddress> addresses) {
        callback_called = true;
        callback_host_name = host_name;
        callback_addresses = std::move(addresses);
      });
  SetAgent(under_test);
  SetLocalHostAddresses(kHostAddresses);

  under_test.Start(kLocalHostFullName);

  // Expect the callback has been called.
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(kLocalHostName, callback_host_name);
  EXPECT_TRUE(VerifyAddresses(kHostAddresses, callback_addresses));

  // Expect the resolver to schedule its own removal.
  ExpectPostTaskForTime(zx::sec(0), zx::sec(0));

  ExpectNoOther();
}

// Tests resolution of a local proxy host.
TEST_F(HostNameResolverTest, LocalProxyHost) {
  bool callback_called = false;
  std::string callback_host_name;
  std::vector<HostAddress> callback_addresses;
  HostNameResolver under_test(
      this, kHostName, Media::kBoth, IpVersions::kBoth, kExcludeLocal, kIncludeLocalProxies,
      timeout,
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

  // Expect the callback has not been called.
  EXPECT_FALSE(callback_called);

  under_test.OnAddProxyHost(kHostFullName, kHostAddresses);

  // Expect the callback has been called.
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(kHostName, callback_host_name);
  EXPECT_TRUE(VerifyAddresses(kHostAddresses, callback_addresses));

  // Expect the resolver to schedule its own removal.
  ExpectPostTaskForTime(zx::sec(0), zx::sec(0));

  ExpectNoOther();
}

// Tests that a local proxy host is not used if that feature is off.
TEST_F(HostNameResolverTest, NoLocalProxyHost) {
  bool callback_called = false;
  std::string callback_host_name;
  std::vector<HostAddress> callback_addresses;
  HostNameResolver under_test(
      this, kHostName, Media::kBoth, IpVersions::kBoth, kExcludeLocal, kExcludeLocalProxies,
      timeout,
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

  // Expect the callback has not been called.
  EXPECT_FALSE(callback_called);

  under_test.OnAddProxyHost(kHostFullName, kHostAddresses);

  EXPECT_FALSE(callback_called);
}

}  // namespace test
}  // namespace mdns
