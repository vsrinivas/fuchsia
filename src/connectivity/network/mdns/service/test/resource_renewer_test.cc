// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/resource_renewer.h"

#include <lib/zx/time.h>

#include <gtest/gtest.h>

#include "src/connectivity/network/mdns/service/test/agent_test.h"

namespace mdns {
namespace test {

class ResourceRenewerTest : public AgentTest {
 public:
  ResourceRenewerTest() = default;
};

constexpr uint32_t kTimeToLiveSeconds = 120;
constexpr uint32_t kInterfaceId = 1;

// Tests behavior of the requestor when a resource expires.
TEST_F(ResourceRenewerTest, Expiration) {
  ResourceRenewer under_test(this);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);
  ExpectNoOther();

  DnsResource resource(kLocalHostFullName, inet::IpAddress(192, 168, 1, 1));
  resource.time_to_live_ = kTimeToLiveSeconds;
  under_test.Renew(resource, Media::kBoth, IpVersions::kBoth);

  // First renewal is at 80% of TTL.
  zx::duration interval = zx::msec(kTimeToLiveSeconds * 800);
  ExpectPostTaskForTimeAndInvoke(interval, interval);

  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kLocalHostFullName, DnsType::kA);
  ExpectNoOtherQuestionOrResource(message.get());

  // Second renewal is at 85% of TTL.
  interval = zx::msec(kTimeToLiveSeconds * 50);
  ExpectPostTaskForTimeAndInvoke(interval, interval);

  message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kLocalHostFullName, DnsType::kA);
  ExpectNoOtherQuestionOrResource(message.get());

  // Third renewal is at 90% of TTL.
  interval = zx::msec(kTimeToLiveSeconds * 50);
  ExpectPostTaskForTimeAndInvoke(interval, interval);

  message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kLocalHostFullName, DnsType::kA);
  ExpectNoOtherQuestionOrResource(message.get());

  // Fourth renewal interval is at 95% of TTL.
  interval = zx::msec(kTimeToLiveSeconds * 50);
  ExpectPostTaskForTimeAndInvoke(interval, interval);

  message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kLocalHostFullName, DnsType::kA);
  ExpectNoOtherQuestionOrResource(message.get());

  // Expiration is at TTL.
  interval = zx::msec(kTimeToLiveSeconds * 50);
  ExpectPostTaskForTimeAndInvoke(interval, interval);

  auto expired = DnsResource(kLocalHostFullName, DnsType::kA);
  expired.time_to_live_ = 0;
  ExpectExpiration(expired);
  ExpectNoOther();
}

// Tests behavior of the requestor when a resource is renewed.
TEST_F(ResourceRenewerTest, Renewal) {
  ResourceRenewer under_test(this);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);
  ExpectNoOther();

  DnsResource resource(kLocalHostFullName, inet::IpAddress(192, 168, 1, 1));
  resource.time_to_live_ = kTimeToLiveSeconds;
  under_test.Renew(resource, Media::kBoth, IpVersions::kBoth);

  // First renewal is at 80% of TTL.
  zx::duration interval = zx::msec(kTimeToLiveSeconds * 800);
  ExpectPostTaskForTimeAndInvoke(interval, interval);

  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kLocalHostFullName, DnsType::kA);
  ExpectNoOtherQuestionOrResource(message.get());

  ReplyAddress sender_address(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  under_test.ReceiveResource(resource, MdnsResourceSection::kAnswer, sender_address);

  // Second renewal is at 85% of TTL. Because the resource was renewed, we expect it to be
  // forgotten, and we won't see the second query.
  interval = zx::msec(kTimeToLiveSeconds * 50);
  ExpectPostTaskForTimeAndInvoke(interval, interval);
  ExpectNoOther();
}

// Tests behavior of the requestor when renewing a resource for wireless only.
TEST_F(ResourceRenewerTest, WirelessOnly) {
  ResourceRenewer under_test(this);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);
  ExpectNoOther();

  DnsResource resource(kLocalHostFullName, inet::IpAddress(192, 168, 1, 1));
  resource.time_to_live_ = kTimeToLiveSeconds;
  under_test.Renew(resource, Media::kWireless, IpVersions::kBoth);

  // First renewal is at 80% of TTL.
  zx::duration interval = zx::msec(kTimeToLiveSeconds * 800);
  ExpectPostTaskForTimeAndInvoke(interval, interval);

  auto message =
      ExpectOutboundMessage(ReplyAddress::Multicast(Media::kWireless, IpVersions::kBoth));
  ExpectQuestion(message.get(), kLocalHostFullName, DnsType::kA);
  ExpectNoOtherQuestionOrResource(message.get());

  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWired, IpVersions::kV4);
  under_test.ReceiveResource(resource, MdnsResourceSection::kAnswer, sender_address0);

  // Second renewal is at 85% of TTL. We expect to see the query, because the sender address
  // of the received resource wasn't wireless.
  interval = zx::msec(kTimeToLiveSeconds * 50);
  ExpectPostTaskForTimeAndInvoke(interval, interval);

  message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kWireless, IpVersions::kBoth));
  ExpectQuestion(message.get(), kLocalHostFullName, DnsType::kA);
  ExpectNoOtherQuestionOrResource(message.get());

  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  under_test.ReceiveResource(resource, MdnsResourceSection::kAnswer, sender_address1);

  // Third renewal is at 90% of TTL. Because the resource was renewed, we expect it to be
  // forgotten, and we won't see the third query.
  interval = zx::msec(kTimeToLiveSeconds * 50);
  ExpectPostTaskForTimeAndInvoke(interval, interval);
  ExpectNoOther();
}

// Tests behavior of the requestor when renewing a resource for wired only.
TEST_F(ResourceRenewerTest, WiredOnly) {
  ResourceRenewer under_test(this);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);
  ExpectNoOther();

  DnsResource resource(kLocalHostFullName, inet::IpAddress(192, 168, 1, 1));
  resource.time_to_live_ = kTimeToLiveSeconds;
  under_test.Renew(resource, Media::kWired, IpVersions::kBoth);

  // First renewal is at 80% of TTL.
  zx::duration interval = zx::msec(kTimeToLiveSeconds * 800);
  ExpectPostTaskForTimeAndInvoke(interval, interval);

  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kWired, IpVersions::kBoth));
  ExpectQuestion(message.get(), kLocalHostFullName, DnsType::kA);
  ExpectNoOtherQuestionOrResource(message.get());

  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  under_test.ReceiveResource(resource, MdnsResourceSection::kAnswer, sender_address0);

  // Second renewal is at 85% of TTL. We expect to see the query, because the sender address
  // of the received resource wasn't wired.
  interval = zx::msec(kTimeToLiveSeconds * 50);
  ExpectPostTaskForTimeAndInvoke(interval, interval);

  message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kWired, IpVersions::kBoth));
  ExpectQuestion(message.get(), kLocalHostFullName, DnsType::kA);
  ExpectNoOtherQuestionOrResource(message.get());

  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWired, IpVersions::kV4);
  under_test.ReceiveResource(resource, MdnsResourceSection::kAnswer, sender_address1);

  // Third renewal is at 90% of TTL. Because the resource was renewed, we expect it to be
  // forgotten, and we won't see the third query.
  interval = zx::msec(kTimeToLiveSeconds * 50);
  ExpectPostTaskForTimeAndInvoke(interval, interval);
  ExpectNoOther();
}

// Tests behavior of the requestor when renewing a resource for IPv4 only.
TEST_F(ResourceRenewerTest, V4Only) {
  ResourceRenewer under_test(this);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);
  ExpectNoOther();

  DnsResource resource(kLocalHostFullName, inet::IpAddress(192, 168, 1, 1));
  resource.time_to_live_ = kTimeToLiveSeconds;
  under_test.Renew(resource, Media::kBoth, IpVersions::kV4);

  // First renewal is at 80% of TTL.
  zx::duration interval = zx::msec(kTimeToLiveSeconds * 800);
  ExpectPostTaskForTimeAndInvoke(interval, interval);

  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kV4));
  ExpectQuestion(message.get(), kLocalHostFullName, DnsType::kA);
  ExpectNoOtherQuestionOrResource(message.get());

  ReplyAddress sender_address0(inet::SocketAddress(0xfe80, 1, inet::IpPort::From_uint16_t(5353)),
                               inet::IpAddress(0xfe80, 100), kInterfaceId, Media::kWireless,
                               IpVersions::kV6);
  under_test.ReceiveResource(resource, MdnsResourceSection::kAnswer, sender_address0);

  // Second renewal is at 85% of TTL. We expect to see the query, because the sender address
  // of the received resource wasn't IPv4.
  interval = zx::msec(kTimeToLiveSeconds * 50);
  ExpectPostTaskForTimeAndInvoke(interval, interval);

  message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kV4));
  ExpectQuestion(message.get(), kLocalHostFullName, DnsType::kA);
  ExpectNoOtherQuestionOrResource(message.get());

  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  under_test.ReceiveResource(resource, MdnsResourceSection::kAnswer, sender_address1);

  // Third renewal is at 90% of TTL. Because the resource was renewed, we expect it to be
  // forgotten, and we won't see the third query.
  interval = zx::msec(kTimeToLiveSeconds * 50);
  ExpectPostTaskForTimeAndInvoke(interval, interval);
  ExpectNoOther();
}

// Tests behavior of the requestor when renewing a resource for IPv6 only.
TEST_F(ResourceRenewerTest, V6Only) {
  ResourceRenewer under_test(this);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);
  ExpectNoOther();

  DnsResource resource(kLocalHostFullName, inet::IpAddress(0xfe80, 1));
  resource.time_to_live_ = kTimeToLiveSeconds;
  under_test.Renew(resource, Media::kBoth, IpVersions::kV6);

  // First renewal is at 80% of TTL.
  zx::duration interval = zx::msec(kTimeToLiveSeconds * 800);
  ExpectPostTaskForTimeAndInvoke(interval, interval);

  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kV6));
  ExpectQuestion(message.get(), kLocalHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());

  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  under_test.ReceiveResource(resource, MdnsResourceSection::kAnswer, sender_address0);

  // Second renewal is at 85% of TTL. We expect to see the query, because the sender address
  // of the received resource wasn't IPv4.
  interval = zx::msec(kTimeToLiveSeconds * 50);
  ExpectPostTaskForTimeAndInvoke(interval, interval);

  message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kV6));
  ExpectQuestion(message.get(), kLocalHostFullName, DnsType::kAaaa);
  ExpectNoOtherQuestionOrResource(message.get());

  ReplyAddress sender_address1(inet::SocketAddress(0xfe80, 1, inet::IpPort::From_uint16_t(5353)),
                               inet::IpAddress(0xfe80, 100), kInterfaceId, Media::kWireless,
                               IpVersions::kV6);
  under_test.ReceiveResource(resource, MdnsResourceSection::kAnswer, sender_address1);

  // Third renewal is at 90% of TTL. Because the resource was renewed, we expect it to be
  // forgotten, and we won't see the third query.
  interval = zx::msec(kTimeToLiveSeconds * 50);
  ExpectPostTaskForTimeAndInvoke(interval, interval);
  ExpectNoOther();
}

}  // namespace test
}  // namespace mdns
