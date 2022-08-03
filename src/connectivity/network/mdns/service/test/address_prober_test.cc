// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/address_prober.h"

#include <lib/zx/time.h>

#include <gtest/gtest.h>

#include "src/connectivity/network/mdns/service/test/agent_test.h"

namespace mdns {
namespace test {

class AddressProberTest : public AgentTest {
 public:
  AddressProberTest() {}

 protected:
  // Expects that a probe for the local host name has been sent.
  void ExpectProbe(Media media, IpVersions ip_versions) {
    auto message = ExpectOutboundMessage(ReplyAddress::Multicast(media, ip_versions));
    ExpectQuestion(message.get(), kLocalHostFullName, DnsType::kAny, DnsClass::kIn, true);
    ExpectAddressPlaceholder(message.get(), MdnsResourceSection::kAuthority);
    ExpectNoOtherQuestionOrResource(message.get());
  }

  // Expects that a probe for the specified host name and addresses has been sent.
  void ExpectProbe(const std::string& host_full_name, const std::vector<inet::IpAddress>& addresses,
                   Media media, IpVersions ip_versions) {
    auto message = ExpectOutboundMessage(ReplyAddress::Multicast(media, ip_versions));
    ExpectQuestion(message.get(), host_full_name, DnsType::kAny, DnsClass::kIn, true);
    ExpectAddresses(message.get(), MdnsResourceSection::kAuthority, host_full_name, addresses);
    ExpectNoOtherQuestionOrResource(message.get());
  }
};

constexpr char kHostFullName[] = "test2host.local.";
constexpr char kAlternateCaseHostFullName[] = "tEst2hOsT.lOcaL.";
const std::vector<inet::IpAddress> kAddresses{inet::IpAddress(192, 168, 1, 200),
                                              inet::IpAddress(192, 168, 1, 201)};
constexpr uint32_t kInterfaceId = 1;

// Tests nominal behavior of the prober when there are no conflicts.
TEST_F(AddressProberTest, Nominal) {
  bool callback_called = false;
  bool callback_success;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<AddressProber>(
      this, Media::kBoth, IpVersions::kBoth, [&callback_called, &callback_success](bool success) {
        callback_called = true;
        callback_success = success;
      });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect three probe messages, the first after a delay of 0~250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(Media::kBoth, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  // ...the second after a delay of 250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(Media::kBoth, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  // ...and the third after a delay of 250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(Media::kBoth, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(callback_success);
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

// Tests behavior of the prober when there is a conflict.
TEST_F(AddressProberTest, Conflict) {
  bool callback_called = false;
  bool callback_success;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<AddressProber>(
      this, Media::kBoth, IpVersions::kBoth, [&callback_called, &callback_success](bool success) {
        callback_called = true;
        callback_success = success;
      });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect the first probe message after a delay of 0~250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(Media::kBoth, IpVersions::kBoth);
  ExpectPostTaskForTime(zx::msec(250), zx::msec(250));
  EXPECT_FALSE(callback_called);

  // Send a reply.
  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, kInterfaceId, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  under_test->ReceiveResource(DnsResource(kLocalHostFullName, inet::IpAddress(192, 168, 1, 1)),
                              MdnsResourceSection::kAnswer, sender_address0);

  // Callback and removal are run in a task.
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(0));

  // Expect the probe to fail (conflict).
  EXPECT_TRUE(callback_called);
  EXPECT_FALSE(callback_success);
  ExpectRemoveAgentCall();
  ExpectNoOther();

  // Redoing test with a case insensitive conflict
  callback_called = false;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  under_test = std::make_shared<AddressProber>(this, Media::kBoth, IpVersions::kBoth,
                                               [&callback_called, &callback_success](bool success) {
                                                 callback_called = true;
                                                 callback_success = success;
                                               });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect the first probe message after a delay of 0~250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(Media::kBoth, IpVersions::kBoth);
  ExpectPostTaskForTime(zx::msec(250), zx::msec(250));
  EXPECT_FALSE(callback_called);

  // Send a reply.
  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, kInterfaceId, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWireless, IpVersions::kV4);
  under_test->ReceiveResource(
      DnsResource(kAlternateCaseLocalHostFullName, inet::IpAddress(192, 168, 1, 1)),
      MdnsResourceSection::kAnswer, sender_address1);

  // Callback and removal are run in a task.
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(0));

  // Expect the probe to fail (conflict).
  EXPECT_TRUE(callback_called);
  EXPECT_FALSE(callback_success);
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

// Tests nominal behavior of the prober configured for wired-only.
TEST_F(AddressProberTest, NominalWiredOnly) {
  bool callback_called = false;
  bool callback_success;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<AddressProber>(
      this, Media::kWired, IpVersions::kBoth, [&callback_called, &callback_success](bool success) {
        callback_called = true;
        callback_success = success;
      });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect three probe messages, the first after a delay of 0~250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(Media::kWired, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  // ...the second after a delay of 250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(Media::kWired, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  // ...and the third after a delay of 250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(Media::kWired, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(callback_success);
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

// Tests nominal behavior of the prober configured for wireless-only.
TEST_F(AddressProberTest, NominalWirelessOnly) {
  bool callback_called = false;
  bool callback_success;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  auto under_test =
      std::make_shared<AddressProber>(this, Media::kWireless, IpVersions::kBoth,
                                      [&callback_called, &callback_success](bool success) {
                                        callback_called = true;
                                        callback_success = success;
                                      });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect three probe messages, the first after a delay of 0~250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(Media::kWireless, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  // ...the second after a delay of 250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(Media::kWireless, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  // ...and the third after a delay of 250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(Media::kWireless, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(callback_success);
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

// Tests nominal behavior of the prober configured for IPv4 only.
TEST_F(AddressProberTest, NominalV4) {
  bool callback_called = false;
  bool callback_success;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<AddressProber>(
      this, Media::kBoth, IpVersions::kV4, [&callback_called, &callback_success](bool success) {
        callback_called = true;
        callback_success = success;
      });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect three probe messages, the first after a delay of 0~250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(Media::kBoth, IpVersions::kV4);
  EXPECT_FALSE(callback_called);

  // ...the second after a delay of 250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(Media::kBoth, IpVersions::kV4);
  EXPECT_FALSE(callback_called);

  // ...and the third after a delay of 250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(Media::kBoth, IpVersions::kV4);
  EXPECT_FALSE(callback_called);

  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(callback_success);
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

// Tests nominal behavior of the prober configured for IPv6 only.
TEST_F(AddressProberTest, NominalV6) {
  bool callback_called = false;
  bool callback_success;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<AddressProber>(
      this, Media::kBoth, IpVersions::kV6, [&callback_called, &callback_success](bool success) {
        callback_called = true;
        callback_success = success;
      });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect three probe messages, the first after a delay of 0~250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(Media::kBoth, IpVersions::kV6);
  EXPECT_FALSE(callback_called);

  // ...the second after a delay of 250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(Media::kBoth, IpVersions::kV6);
  EXPECT_FALSE(callback_called);

  // ...and the third after a delay of 250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(Media::kBoth, IpVersions::kV6);
  EXPECT_FALSE(callback_called);

  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(callback_success);
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

// Tests nominal behavior of the prober created with explicit host name and addresses..
TEST_F(AddressProberTest, HostNameAndAddresses) {
  bool callback_called = false;
  bool callback_success;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<AddressProber>(
      this, kHostFullName, kAddresses, Media::kBoth, IpVersions::kBoth,
      [&callback_called, &callback_success](bool success) {
        callback_called = true;
        callback_success = success;
      });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect three probe messages, the first after a delay of 0~250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(kHostFullName, kAddresses, Media::kBoth, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  // ...the second after a delay of 250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(kHostFullName, kAddresses, Media::kBoth, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  // ...and the third after a delay of 250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(kHostFullName, kAddresses, Media::kBoth, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(callback_success);
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

// Tests behavior of a wired-only/IPv6-only prober when there is a conflict.
TEST_F(AddressProberTest, ConflictWiredV6) {
  bool callback_called = false;
  bool callback_success;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<AddressProber>(
      this, kHostFullName, kAddresses, Media::kWired, IpVersions::kV6,
      [&callback_called, &callback_success](bool success) {
        callback_called = true;
        callback_success = success;
      });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect the first probe message after a delay of 0~250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(kHostFullName, kAddresses, Media::kWired, IpVersions::kV6);
  ExpectPostTaskForTime(zx::msec(250), zx::msec(250));
  EXPECT_FALSE(callback_called);

  // Send a reply wired/V4. Expect it to be ignored, because it's V4
  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), kInterfaceId, Media::kWired, IpVersions::kV4);
  under_test->ReceiveResource(DnsResource(kHostFullName, inet::IpAddress(192, 168, 1, 1)),
                              MdnsResourceSection::kAnswer, sender_address0);
  EXPECT_FALSE(callback_called);
  ExpectNoOther();

  // Send a reply wireless/V6. Expect it to be ignored, because it's wireless
  ReplyAddress sender_address1(inet::SocketAddress(0xfe80, 1, inet::IpPort::From_uint16_t(5353)),
                               inet::IpAddress(0xfe80, 100), kInterfaceId, Media::kWireless,
                               IpVersions::kV6);
  under_test->ReceiveResource(DnsResource(kHostFullName, inet::IpAddress(0xfe80, 1)),
                              MdnsResourceSection::kAnswer, sender_address1);
  EXPECT_FALSE(callback_called);
  ExpectNoOther();

  // Send a reply wired/V6. Expect it to be ignored, because it's wireless
  ReplyAddress sender_address2(inet::SocketAddress(0xfe80, 1, inet::IpPort::From_uint16_t(5353)),
                               inet::IpAddress(0xfe80, 100), kInterfaceId, Media::kWired,
                               IpVersions::kV6);
  under_test->ReceiveResource(DnsResource(kHostFullName, inet::IpAddress(0xfe80, 1)),
                              MdnsResourceSection::kAnswer, sender_address2);

  // Callback and removal are run in a task.
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(0));

  // Expect the probe to fail (conflict).
  EXPECT_TRUE(callback_called);
  EXPECT_FALSE(callback_success);
  ExpectRemoveAgentCall();
  ExpectNoOther();

  // Reset Variables and test case insensitive hostname conflict
  callback_called = false;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  under_test = std::make_shared<AddressProber>(this, kHostFullName, kAddresses, Media::kWired,
                                               IpVersions::kV6,
                                               [&callback_called, &callback_success](bool success) {
                                                 callback_called = true;
                                                 callback_success = success;
                                               });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect the first probe message after a delay of 0~250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(kHostFullName, kAddresses, Media::kWired, IpVersions::kV6);
  ExpectPostTaskForTime(zx::msec(250), zx::msec(250));
  EXPECT_FALSE(callback_called);

  // Send a reply wired/V6.
  ReplyAddress sender_address3(inet::SocketAddress(0xfe80, 1, inet::IpPort::From_uint16_t(5353)),
                               inet::IpAddress(0xfe80, 100), kInterfaceId, Media::kWired,
                               IpVersions::kV6);
  under_test->ReceiveResource(DnsResource(kAlternateCaseHostFullName, inet::IpAddress(0xfe80, 1)),
                              MdnsResourceSection::kAnswer, sender_address3);

  // Callback and removal are run in a task.
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(0));

  // Expect the probe to fail (conflict).
  EXPECT_TRUE(callback_called);
  EXPECT_FALSE(callback_success);
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

}  // namespace test
}  // namespace mdns
