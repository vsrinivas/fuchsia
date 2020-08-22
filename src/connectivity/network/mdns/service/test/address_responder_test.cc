// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/address_responder.h"

#include <lib/zx/time.h>

#include <gtest/gtest.h>

#include "src/connectivity/network/mdns/service/test/agent_test.h"

namespace mdns {
namespace test {

class AddressResponderTest : public AgentTest {
 public:
  AddressResponderTest() {}

 protected:
  // Expects that addresses are transmitted to the multicast address.
  void ExpectAddresses();

 private:
};

void AddressResponderTest::ExpectAddresses() {
  auto message = ExpectOutboundMessage(addresses().multicast_reply());
  ExpectAddressPlaceholder(message.get(), MdnsResourceSection::kAnswer);
  ExpectNoOtherQuestionOrResource(message.get());
}

// Tests initial startup of the responder.
TEST_F(AddressResponderTest, Startup) {
  AddressResponder under_test(this);
  SetAgent(under_test);

  under_test.Start(kHostFullName, addresses());
  // No initial action.
  ExpectNoOther();
}

// Tests that multicast sends are rate-limited.
TEST_F(AddressResponderTest, MulticastRateLimit) {
  AddressResponder under_test(this);
  SetAgent(under_test);

  // Normal startup.
  under_test.Start(kHostFullName, addresses());
  ExpectNoOther();

  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), Media::kBoth);
  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 2, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), Media::kBoth);

  // First question.
  under_test.ReceiveQuestion(DnsQuestion(kHostFullName, DnsType::kA), addresses().multicast_reply(),
                             sender_address0);
  ExpectAddresses();
  ExpectNoOther();

  // Second question - answer should be delayed 1s.
  under_test.ReceiveQuestion(DnsQuestion(kHostFullName, DnsType::kA), addresses().multicast_reply(),
                             sender_address0);
  ExpectPostTaskForTimeAndInvoke(zx::sec(1), zx::sec(1));
  ExpectAddresses();
  ExpectNoOther();

  // Third and fourth questions - one answer, delayed 1s.
  under_test.ReceiveQuestion(DnsQuestion(kHostFullName, DnsType::kA), addresses().multicast_reply(),
                             sender_address0);
  under_test.ReceiveQuestion(DnsQuestion(kHostFullName, DnsType::kA), addresses().multicast_reply(),
                             sender_address1);
  ExpectPostTaskForTimeAndInvoke(zx::sec(1), zx::sec(1));
  ExpectAddresses();
  ExpectNoOther();
}

}  // namespace test
}  // namespace mdns
