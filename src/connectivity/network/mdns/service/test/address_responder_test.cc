// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/address_responder.h"

#include <lib/zx/time.h>

#include <gtest/gtest.h>

#include "src/connectivity/network/mdns/service/test/agent_test.h"

namespace mdns {
namespace test {

class AddressResponderTest : public AgentTest {
 public:
  AddressResponderTest() {}

 protected:
  // Expects that local addresses are transmitted to the multicast address.
  void ExpectAddresses(Media media, IpVersions ip_versions) {
    auto message = ExpectOutboundMessage(ReplyAddress::Multicast(media, ip_versions));
    ExpectAddressPlaceholder(message.get(), MdnsResourceSection::kAnswer);
    ExpectNoOtherQuestionOrResource(message.get());
  }

  // Expects that |addresses| for |host_full_name| are transmitted to the multicast address.
  void ExpectAddresses(const std::string& host_full_name,
                       const std::vector<inet::IpAddress>& addresses, Media media,
                       IpVersions ip_versions) {
    auto message = ExpectOutboundMessage(ReplyAddress::Multicast(media, ip_versions));
    AgentTest::ExpectAddresses(message.get(), MdnsResourceSection::kAnswer, host_full_name,
                               addresses);
    ExpectNoOtherQuestionOrResource(message.get());
  }
};

constexpr char kHostFullName[] = "test2host.local.";
const std::vector<inet::IpAddress> kAddresses{inet::IpAddress(192, 168, 1, 200),
                                              inet::IpAddress(192, 168, 1, 201)};

// Tests initial startup of the responder.
TEST_F(AddressResponderTest, Startup) {
  AddressResponder under_test(this, Media::kBoth, IpVersions::kBoth);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);
  // No initial action.
  ExpectNoOther();
}

// Tests that multicast sends are rate-limited.
TEST_F(AddressResponderTest, MulticastRateLimit) {
  AddressResponder under_test(this, Media::kBoth, IpVersions::kBoth);
  SetAgent(under_test);

  // Normal startup.
  under_test.Start(kLocalHostFullName);
  ExpectNoOther();

  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), Media::kWireless, IpVersions::kV4);
  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 2, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(0xfe80, 1), Media::kWired, IpVersions::kV6);

  // First question.
  under_test.ReceiveQuestion(DnsQuestion(kLocalHostFullName, DnsType::kA),
                             ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth),
                             sender_address0);
  ExpectAddresses(Media::kBoth, IpVersions::kBoth);
  ExpectNoOther();

  // Second question - answer should be delayed 1s.
  under_test.ReceiveQuestion(DnsQuestion(kLocalHostFullName, DnsType::kA),
                             ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth),
                             sender_address0);
  ExpectPostTaskForTimeAndInvoke(zx::sec(1), zx::sec(1));
  ExpectAddresses(Media::kBoth, IpVersions::kBoth);
  ExpectNoOther();

  // Third and fourth questions - one answer, delayed 1s.
  under_test.ReceiveQuestion(DnsQuestion(kLocalHostFullName, DnsType::kA),
                             ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth),
                             sender_address0);
  under_test.ReceiveQuestion(DnsQuestion(kLocalHostFullName, DnsType::kA),
                             ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth),
                             sender_address1);
  ExpectPostTaskForTimeAndInvoke(zx::sec(1), zx::sec(1));
  ExpectAddresses(Media::kBoth, IpVersions::kBoth);
  ExpectNoOther();
}

// Tests operation with a explicit host name and addresses.
TEST_F(AddressResponderTest, HostNameAndAddresses) {
  AddressResponder under_test(this, kHostFullName, kAddresses, Media::kBoth, IpVersions::kBoth);
  SetAgent(under_test);

  // Normal startup.
  under_test.Start(kLocalHostFullName);
  ExpectNoOther();

  ReplyAddress sender_address(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), Media::kWireless, IpVersions::kV4);

  under_test.ReceiveQuestion(DnsQuestion(kHostFullName, DnsType::kA),
                             ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth),
                             sender_address);
  ExpectAddresses(kHostFullName, kAddresses, Media::kBoth, IpVersions::kBoth);
  ExpectNoOther();
}

// Tests operation with wired media only.
TEST_F(AddressResponderTest, WiredOnly) {
  AddressResponder under_test(this, kHostFullName, kAddresses, Media::kWired, IpVersions::kBoth);
  SetAgent(under_test);

  // Normal startup.
  under_test.Start(kLocalHostFullName);
  ExpectNoOther();

  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), Media::kWireless, IpVersions::kV4);

  under_test.ReceiveQuestion(DnsQuestion(kHostFullName, DnsType::kA),
                             ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth),
                             sender_address0);
  // Expect no response, because the sender address is wireless.
  ExpectNoOther();

  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), Media::kWired, IpVersions::kBoth);

  under_test.ReceiveQuestion(DnsQuestion(kHostFullName, DnsType::kA),
                             ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth),
                             sender_address1);
  ExpectAddresses(kHostFullName, kAddresses, Media::kWired, IpVersions::kBoth);
  ExpectNoOther();
}

// Tests operation with wireless media only.
TEST_F(AddressResponderTest, WirelessOnly) {
  AddressResponder under_test(this, kHostFullName, kAddresses, Media::kWireless, IpVersions::kBoth);
  SetAgent(under_test);

  // Normal startup.
  under_test.Start(kLocalHostFullName);
  ExpectNoOther();

  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), Media::kWired, IpVersions::kV4);

  under_test.ReceiveQuestion(DnsQuestion(kHostFullName, DnsType::kA),
                             ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth),
                             sender_address0);
  // Expect no response, because the sender address is wired.
  ExpectNoOther();

  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), Media::kWireless, IpVersions::kBoth);

  under_test.ReceiveQuestion(DnsQuestion(kHostFullName, DnsType::kA),
                             ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth),
                             sender_address1);
  ExpectAddresses(kHostFullName, kAddresses, Media::kWireless, IpVersions::kBoth);
  ExpectNoOther();
}

// Tests operation with V4 ip_version only.
TEST_F(AddressResponderTest, V4Only) {
  AddressResponder under_test(this, kHostFullName, kAddresses, Media::kBoth, IpVersions::kV4);
  SetAgent(under_test);

  // Normal startup.
  under_test.Start(kLocalHostFullName);
  ExpectNoOther();

  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(0xfe80, 1), Media::kWireless, IpVersions::kV6);

  under_test.ReceiveQuestion(DnsQuestion(kHostFullName, DnsType::kA),
                             ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth),
                             sender_address0);
  // Expect no response, because the sender address is V6.
  ExpectNoOther();

  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), Media::kWired, IpVersions::kV4);

  under_test.ReceiveQuestion(DnsQuestion(kHostFullName, DnsType::kA),
                             ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth),
                             sender_address1);
  ExpectAddresses(kHostFullName, kAddresses, Media::kBoth, IpVersions::kV4);
  ExpectNoOther();
}

// Tests operation with V6 ip_version only.
TEST_F(AddressResponderTest, V6Only) {
  AddressResponder under_test(this, kHostFullName, kAddresses, Media::kBoth, IpVersions::kV6);
  SetAgent(under_test);

  // Normal startup.
  under_test.Start(kLocalHostFullName);
  ExpectNoOther();

  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), Media::kWireless, IpVersions::kV4);

  under_test.ReceiveQuestion(DnsQuestion(kHostFullName, DnsType::kA),
                             ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth),
                             sender_address0);
  // Expect no response, because the sender address is V4.
  ExpectNoOther();

  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(0xfe80, 1), Media::kWired, IpVersions::kV6);

  under_test.ReceiveQuestion(DnsQuestion(kHostFullName, DnsType::kA),
                             ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth),
                             sender_address1);
  ExpectAddresses(kHostFullName, kAddresses, Media::kBoth, IpVersions::kV6);
  ExpectNoOther();
}

}  // namespace test
}  // namespace mdns
