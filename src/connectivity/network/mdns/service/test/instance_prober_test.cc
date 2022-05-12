// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/instance_prober.h"

#include <lib/zx/time.h>

#include <gtest/gtest.h>

#include "src/connectivity/network/mdns/service/test/agent_test.h"

namespace mdns {
namespace test {

class InstanceProberTest : public AgentTest {
 public:
  InstanceProberTest() {}

 protected:
  // Expects that a probe for the specified instance on the specified host has been sent.
  void ExpectProbe(const std::string& instance_full_name, const std::string& host_full_name,
                   inet::IpPort port, Media media, IpVersions ip_versions) {
    auto message = ExpectOutboundMessage(ReplyAddress::Multicast(media, ip_versions));
    ExpectQuestion(message.get(), instance_full_name, DnsType::kAny, DnsClass::kIn, true);
    auto resource = ExpectResource(message.get(), MdnsResourceSection::kAuthority,
                                   instance_full_name, DnsType::kSrv);
    EXPECT_EQ(port, resource->srv_.port_);
    EXPECT_EQ(host_full_name, resource->srv_.target_.dotted_string_);
    ExpectNoOtherQuestionOrResource(message.get());
  }
};

constexpr char kServiceName[] = "_testservice._tcp.";
constexpr char kInstanceName[] = "testinstance";
constexpr char kInstanceFullName[] = "testinstance._testservice._tcp.local.";
const inet::IpPort kPort = inet::IpPort::From_uint16_t(1234);

// Tests nominal behavior of the prober when there are no conflicts.
TEST_F(InstanceProberTest, Nominal) {
  bool callback_called = false;
  bool callback_success;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<InstanceProber>(
      this, kServiceName, kInstanceName, kLocalHostFullName, kPort, Media::kBoth, IpVersions::kBoth,
      [&callback_called, &callback_success](bool success) {
        callback_called = true;
        callback_success = success;
      });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect three probe messages, the first after a delay of 0~250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kBoth, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  // ...the second after a delay of 250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kBoth, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  // ...and the third after a delay of 250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kBoth, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(callback_success);
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

// Tests behavior of the prober when there is a conflict.
TEST_F(InstanceProberTest, Conflict) {
  bool callback_called = false;
  bool callback_success;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<InstanceProber>(
      this, kServiceName, kInstanceName, kLocalHostFullName, kPort, Media::kBoth, IpVersions::kBoth,
      [&callback_called, &callback_success](bool success) {
        callback_called = true;
        callback_success = success;
      });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect the first probe message after a delay of 0~250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kBoth, IpVersions::kBoth);
  ExpectPostTaskForTime(zx::msec(250), zx::msec(250));
  EXPECT_FALSE(callback_called);

  // Send a reply.
  ReplyAddress sender_address(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), Media::kWireless, IpVersions::kV4);
  DnsResource resource(kInstanceFullName, DnsType::kSrv);
  resource.srv_.port_ = kPort;
  resource.srv_.target_ = kLocalHostFullName;
  under_test->ReceiveResource(resource, MdnsResourceSection::kAnswer, sender_address);

  // Callback and removal are run in a task.
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(0));

  // Expect the probe to fail (conflict).
  EXPECT_TRUE(callback_called);
  EXPECT_FALSE(callback_success);
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

// Tests nominal behavior of the prober configured for wired-only.
TEST_F(InstanceProberTest, NominalWiredOnly) {
  bool callback_called = false;
  bool callback_success;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<InstanceProber>(
      this, kServiceName, kInstanceName, kLocalHostFullName, kPort, Media::kWired,
      IpVersions::kBoth, [&callback_called, &callback_success](bool success) {
        callback_called = true;
        callback_success = success;
      });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect three probe messages, the first after a delay of 0~250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kWired, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  // ...the second after a delay of 250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kWired, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  // ...and the third after a delay of 250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kWired, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(callback_success);
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

// Tests nominal behavior of the prober configured for wireless-only.
TEST_F(InstanceProberTest, NominalWirelessOnly) {
  bool callback_called = false;
  bool callback_success;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<InstanceProber>(
      this, kServiceName, kInstanceName, kLocalHostFullName, kPort, Media::kWireless,
      IpVersions::kBoth, [&callback_called, &callback_success](bool success) {
        callback_called = true;
        callback_success = success;
      });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect three probe messages, the first after a delay of 0~250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kWireless, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  // ...the second after a delay of 250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kWireless, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  // ...and the third after a delay of 250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kWireless, IpVersions::kBoth);
  EXPECT_FALSE(callback_called);

  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(callback_success);
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

// Tests nominal behavior of the prober configured for IPv4 only.
TEST_F(InstanceProberTest, NominalV4) {
  bool callback_called = false;
  bool callback_success;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<InstanceProber>(
      this, kServiceName, kInstanceName, kLocalHostFullName, kPort, Media::kBoth, IpVersions::kV4,
      [&callback_called, &callback_success](bool success) {
        callback_called = true;
        callback_success = success;
      });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect three probe messages, the first after a delay of 0~250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kBoth, IpVersions::kV4);
  EXPECT_FALSE(callback_called);

  // ...the second after a delay of 250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kBoth, IpVersions::kV4);
  EXPECT_FALSE(callback_called);

  // ...and the third after a delay of 250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kBoth, IpVersions::kV4);
  EXPECT_FALSE(callback_called);

  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(callback_success);
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

// Tests nominal behavior of the prober configured for IPv6 only.
TEST_F(InstanceProberTest, NominalV6) {
  bool callback_called = false;
  bool callback_success;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<InstanceProber>(
      this, kServiceName, kInstanceName, kLocalHostFullName, kPort, Media::kBoth, IpVersions::kV6,
      [&callback_called, &callback_success](bool success) {
        callback_called = true;
        callback_success = success;
      });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect three probe messages, the first after a delay of 0~250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kBoth, IpVersions::kV6);
  EXPECT_FALSE(callback_called);

  // ...the second after a delay of 250ms...
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kBoth, IpVersions::kV6);
  EXPECT_FALSE(callback_called);

  // ...and the third after a delay of 250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kBoth, IpVersions::kV6);
  EXPECT_FALSE(callback_called);

  ExpectPostTaskForTimeAndInvoke(zx::msec(250), zx::msec(250));
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(callback_success);
  ExpectRemoveAgentCall();
  ExpectNoOther();
}

// Tests behavior of a wired-only/IPv6-only prober when there is a conflict.
TEST_F(InstanceProberTest, ConflictWiredV6) {
  bool callback_called = false;
  bool callback_success;

  // We need to make_shared here, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<InstanceProber>(
      this, kServiceName, kInstanceName, kLocalHostFullName, kPort, Media::kWired, IpVersions::kV6,
      [&callback_called, &callback_success](bool success) {
        callback_called = true;
        callback_success = success;
      });
  SetAgent(*under_test);

  under_test->Start(kLocalHostFullName);

  // Expect the first probe message after a delay of 0~250ms.
  ExpectPostTaskForTimeAndInvoke(zx::msec(0), zx::msec(250));
  ExpectProbe(kInstanceFullName, kLocalHostFullName, kPort, Media::kWired, IpVersions::kV6);
  ExpectPostTaskForTime(zx::msec(250), zx::msec(250));
  EXPECT_FALSE(callback_called);

  DnsResource resource(kInstanceFullName, DnsType::kSrv);
  resource.srv_.port_ = kPort;
  resource.srv_.target_ = kLocalHostFullName;

  // Send a reply wired/V4. Expect it to be ignored, because it's V4
  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), Media::kWired, IpVersions::kV4);
  under_test->ReceiveResource(resource, MdnsResourceSection::kAnswer, sender_address0);
  EXPECT_FALSE(callback_called);
  ExpectNoOther();

  // Send a reply wireless/V6. Expect it to be ignored, because it's wireless
  ReplyAddress sender_address1(inet::SocketAddress(0xfe80, 1, inet::IpPort::From_uint16_t(5353)),
                               inet::IpAddress(0xfe80, 100), Media::kWireless, IpVersions::kV6);
  under_test->ReceiveResource(resource, MdnsResourceSection::kAnswer, sender_address1);
  EXPECT_FALSE(callback_called);
  ExpectNoOther();

  // Send a reply wired/V6. Expect it to be ignored, because it's wireless
  ReplyAddress sender_address2(inet::SocketAddress(0xfe80, 1, inet::IpPort::From_uint16_t(5353)),
                               inet::IpAddress(0xfe80, 100), Media::kWired, IpVersions::kV6);
  under_test->ReceiveResource(resource, MdnsResourceSection::kAnswer, sender_address2);

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
