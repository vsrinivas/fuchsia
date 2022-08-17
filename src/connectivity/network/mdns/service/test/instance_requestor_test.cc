// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/agents/instance_requestor.h"

#include <lib/zx/time.h>

#include <gtest/gtest.h>

#include "src/connectivity/network/mdns/service/common/mdns_names.h"
#include "src/connectivity/network/mdns/service/common/service_instance.h"
#include "src/connectivity/network/mdns/service/common/type_converters.h"
#include "src/connectivity/network/mdns/service/test/agent_test.h"

namespace mdns {
namespace test {

class InstanceRequestorTest : public AgentTest {
 public:
  InstanceRequestorTest() = default;

 protected:
  class Subscriber : public Mdns::Subscriber {
   public:
    struct InstanceId {
      InstanceId(const std::string& service, const std::string& instance)
          : service_(service), instance_(instance) {}

      std::string service_;
      std::string instance_;
    };

    void InstanceDiscovered(const std::string& service, const std::string& instance,
                            const std::vector<inet::SocketAddress>& addresses,
                            const std::vector<std::vector<uint8_t>>& text, uint16_t srv_priority,
                            uint16_t srv_weight, const std::string& target) {
      instance_discovered_params_ = std::make_unique<ServiceInstance>(
          service, instance, target, addresses, text, srv_priority, srv_weight);
    }

    void InstanceChanged(const std::string& service, const std::string& instance,
                         const std::vector<inet::SocketAddress>& addresses,
                         const std::vector<std::vector<uint8_t>>& text, uint16_t srv_priority,
                         uint16_t srv_weight, const std::string& target) {
      instance_changed_params_ = std::make_unique<ServiceInstance>(
          service, instance, target, addresses, text, srv_priority, srv_weight);
    }

    void InstanceLost(const std::string& service, const std::string& instance) {
      instance_lost_params_ = std::make_unique<InstanceId>(service, instance);
    }

    void Query(DnsType type_queried) { query_param_ = type_queried; }

    std::unique_ptr<ServiceInstance> ExpectInstanceDiscoveredCalled() {
      EXPECT_TRUE(!!instance_discovered_params_);
      return std::move(instance_discovered_params_);
    }

    std::unique_ptr<ServiceInstance> ExpectInstanceChangedCalled() {
      EXPECT_TRUE(!!instance_changed_params_);
      return std::move(instance_changed_params_);
    }

    std::unique_ptr<InstanceId> ExpectInstanceLostCalled() {
      EXPECT_TRUE(!!instance_lost_params_);
      return std::move(instance_lost_params_);
    }

    void ExpectQueryCalled(DnsType type) {
      EXPECT_NE(DnsType::kInvalid, query_param_);
      EXPECT_EQ(type, query_param_);
      query_param_ = DnsType::kInvalid;
    }

    void ExpectNoOther() {
      EXPECT_FALSE(!!instance_discovered_params_);
      EXPECT_FALSE(!!instance_changed_params_);
      EXPECT_FALSE(!!instance_lost_params_);
      EXPECT_EQ(DnsType::kInvalid, query_param_);
    }

   private:
    std::unique_ptr<ServiceInstance> instance_discovered_params_;
    std::unique_ptr<ServiceInstance> instance_changed_params_;
    std::unique_ptr<InstanceId> instance_lost_params_;
    DnsType query_param_ = DnsType::kInvalid;
  };

  void ReceivePublication(InstanceRequestor& under_test, const std::string& host_full_name,
                          const std::string& service_name, const std::string& instance_name,
                          inet::IpPort port, const std::vector<std::vector<uint8_t>>& text,
                          ReplyAddress sender_address) {
    auto service_full_name = MdnsNames::ServiceFullName(service_name);
    auto instance_full_name = MdnsNames::InstanceFullName(instance_name, service_name);

    DnsResource ptr_resource(service_full_name, DnsType::kPtr);
    ptr_resource.ptr_.pointer_domain_name_ = DnsName(instance_full_name);
    under_test.ReceiveResource(ptr_resource, MdnsResourceSection::kAnswer, sender_address);

    DnsResource srv_resource(instance_full_name, DnsType::kSrv);
    srv_resource.srv_.port_ = port;
    srv_resource.srv_.target_ = DnsName(host_full_name);
    under_test.ReceiveResource(srv_resource, MdnsResourceSection::kAnswer, sender_address);

    DnsResource txt_resource(instance_full_name, DnsType::kTxt);
    txt_resource.txt_.strings_ = text;
    under_test.ReceiveResource(txt_resource, MdnsResourceSection::kAnswer, sender_address);

    DnsResource a_resource(host_full_name, sender_address.socket_address().address());
    under_test.ReceiveResource(a_resource, MdnsResourceSection::kAnswer, sender_address);

    under_test.EndOfMessage();
  }
};

const zx::duration kMinDelay = zx::sec(1);
const zx::duration kMaxDelay = zx::hour(1);
constexpr char kHostFullName[] = "test2host.local.";
constexpr char kHostName[] = "test2host";
constexpr char kServiceName[] = "_testservice._tcp.";
constexpr char kServiceFullName[] = "_testservice._tcp.local.";
constexpr char kInstanceName[] = "testinstance";
constexpr char kInstanceFullName[] = "testinstance._testservice._tcp.local.";
const inet::IpPort kPort = inet::IpPort::From_uint16_t(1234);
const std::vector<std::vector<uint8_t>> kText = fidl::To<std::vector<std::vector<uint8_t>>>(
    std::vector<std::string>{"color=red", "shape=round"});
const std::vector<std::vector<uint8_t>> kAltText = fidl::To<std::vector<std::vector<uint8_t>>>(
    std::vector<std::string>{"color=green", "shape=square"});
constexpr bool kIncludeLocal = true;
constexpr bool kExcludeLocal = false;
constexpr bool kIncludeLocalProxies = true;
constexpr bool kExcludeLocalProxies = false;
constexpr bool kFromLocalProxyHost = true;
constexpr bool kFromLocalHost = false;
const std::vector<HostAddress> kHostAddresses{
    HostAddress(inet::IpAddress(192, 168, 1, 200), 1, zx::sec(450)),
    HostAddress(inet::IpAddress(0xfe80, 200), 1, zx::sec(450))};
const std::vector<inet::SocketAddress> kSocketAddresses{
    inet::SocketAddress(inet::IpAddress(192, 168, 1, 200), kPort, 1),
    inet::SocketAddress(inet::IpAddress(0xfe80, 200), kPort, 1)};
const std::vector<inet::SocketAddress> kSocketAddressesReversed{
    inet::SocketAddress(inet::IpAddress(0xfe80, 200), kPort, 1),
    inet::SocketAddress(inet::IpAddress(192, 168, 1, 200), kPort, 1)};

// Tests nominal startup behavior of the requestor.
TEST_F(InstanceRequestorTest, QuerySequence) {
  InstanceRequestor under_test(this, kServiceName, Media::kBoth, IpVersions::kBoth, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  under_test.Start(kLocalHostFullName);

  // Expect a PTR question on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kServiceFullName, DnsType::kPtr);
  ExpectNoOtherQuestionOrResource(message.get());

  // Queries should be send after 1s, 2s, 4s, etc, capping out an an hour.
  auto delay = kMinDelay;

  while (delay < kMaxDelay) {
    ExpectPostTaskForTimeAndInvoke(delay, delay);
    auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
    ExpectQuestion(message.get(), kServiceFullName, DnsType::kPtr);
    ExpectNoOtherQuestionOrResource(message.get());

    delay = delay * 2;
  }

  ExpectPostTaskForTime(kMaxDelay, kMaxDelay);
  ExpectNoOther();
}

// Tests the behavior of the requestor when a response is received.
TEST_F(InstanceRequestorTest, Response) {
  InstanceRequestor under_test(this, kServiceName, Media::kBoth, IpVersions::kBoth, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);

  under_test.Start(kLocalHostFullName);

  // Expect a PTR question on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kServiceFullName, DnsType::kPtr);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectPostTaskForTime(kMinDelay, kMinDelay);
  ExpectNoOther();

  subscriber.ExpectQueryCalled(DnsType::kPtr);
  subscriber.ExpectNoOther();

  ReplyAddress sender_address(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), 1, Media::kWireless, IpVersions::kV4);
  ReceivePublication(under_test, kHostFullName, kServiceName, kInstanceName, kPort, kText,
                     sender_address);

  auto params = subscriber.ExpectInstanceDiscoveredCalled();
  EXPECT_EQ(ServiceInstance(kServiceName, kInstanceName, kHostName,
                            {inet::SocketAddress(sender_address.socket_address().address(), kPort)},
                            kText, 0, 0),
            *params);

  subscriber.ExpectNoOther();
}

// Tests the behavior of the requestor when responses indicate a change to an instance.
TEST_F(InstanceRequestorTest, Change) {
  InstanceRequestor under_test(this, kServiceName, Media::kBoth, IpVersions::kBoth, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);

  under_test.Start(kLocalHostFullName);

  // Expect a PTR question on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kServiceFullName, DnsType::kPtr);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectPostTaskForTime(kMinDelay, kMinDelay);
  ExpectNoOther();

  subscriber.ExpectQueryCalled(DnsType::kPtr);
  subscriber.ExpectNoOther();

  // Respond.
  ReplyAddress sender_address(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), 1, Media::kWireless, IpVersions::kV4);
  ReceivePublication(under_test, kHostFullName, kServiceName, kInstanceName, kPort, kText,
                     sender_address);

  auto params = subscriber.ExpectInstanceDiscoveredCalled();
  EXPECT_EQ(ServiceInstance(kServiceName, kInstanceName, kHostName,
                            {inet::SocketAddress(sender_address.socket_address().address(), kPort)},
                            kText, 0, 0),
            *params);

  subscriber.ExpectNoOther();

  // Respond with different text.
  ReceivePublication(under_test, kHostFullName, kServiceName, kInstanceName, kPort, kAltText,
                     sender_address);

  params = subscriber.ExpectInstanceChangedCalled();
  EXPECT_EQ(ServiceInstance(kServiceName, kInstanceName, kHostName,
                            {inet::SocketAddress(sender_address.socket_address().address(), kPort)},
                            kAltText, 0, 0),
            *params);

  subscriber.ExpectNoOther();
}

// Tests the behavior of the requestor when an expiration indicates the removal of an instance.
TEST_F(InstanceRequestorTest, Removal) {
  InstanceRequestor under_test(this, kServiceName, Media::kBoth, IpVersions::kBoth, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);

  under_test.Start(kLocalHostFullName);

  // Expect a PTR question on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kServiceFullName, DnsType::kPtr);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectPostTaskForTime(kMinDelay, kMinDelay);
  ExpectNoOther();

  subscriber.ExpectQueryCalled(DnsType::kPtr);
  subscriber.ExpectNoOther();

  // Respond.
  ReplyAddress sender_address(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), 1, Media::kWireless, IpVersions::kV4);
  ReceivePublication(under_test, kHostFullName, kServiceName, kInstanceName, kPort, kText,
                     sender_address);

  auto params = subscriber.ExpectInstanceDiscoveredCalled();
  EXPECT_EQ(ServiceInstance(kServiceName, kInstanceName, kHostName,
                            {inet::SocketAddress(sender_address.socket_address().address(), kPort)},
                            kText, 0, 0),
            *params);

  subscriber.ExpectNoOther();

  // Expire the PTR resource.
  DnsResource ptr_resource(kServiceFullName, DnsType::kPtr);
  ptr_resource.ptr_.pointer_domain_name_ = DnsName(kInstanceFullName);
  ptr_resource.time_to_live_ = 0;
  under_test.ReceiveResource(ptr_resource, MdnsResourceSection::kExpired, sender_address);

  auto instance_id = subscriber.ExpectInstanceLostCalled();
  EXPECT_EQ(kInstanceName, instance_id->instance_);
  EXPECT_EQ(kServiceName, instance_id->service_);
  subscriber.ExpectNoOther();
}

// Tests that the requstor removed itself when the last subscriber is removed.
TEST_F(InstanceRequestorTest, RemoveSelf) {
  // Need to |make_shared|, because |RemoveSelf| calls |shared_from_this|.
  auto under_test = std::make_shared<InstanceRequestor>(
      this, kServiceName, Media::kBoth, IpVersions::kBoth, kExcludeLocal, kExcludeLocalProxies);
  SetAgent(*under_test);

  Subscriber subscriber;
  under_test->AddSubscriber(&subscriber);
  under_test->RemoveSubscriber(&subscriber);

  ExpectPostTaskForTimeAndInvoke(zx::sec(0), zx::sec(0));
  ExpectRemoveAgentCall();
}

// Tests the behavior of the requestor when configured for wireless-only operation.
TEST_F(InstanceRequestorTest, WirelessOnly) {
  InstanceRequestor under_test(this, kServiceName, Media::kWireless, IpVersions::kBoth,
                               kExcludeLocal, kExcludeLocalProxies);
  SetAgent(under_test);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);

  under_test.Start(kLocalHostFullName);

  // Expect a PTR question on start.
  auto message =
      ExpectOutboundMessage(ReplyAddress::Multicast(Media::kWireless, IpVersions::kBoth));
  ExpectQuestion(message.get(), kServiceFullName, DnsType::kPtr);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectPostTaskForTime(kMinDelay, kMinDelay);
  ExpectNoOther();

  subscriber.ExpectQueryCalled(DnsType::kPtr);
  subscriber.ExpectNoOther();

  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), 1, Media::kWired, IpVersions::kV4);
  ReceivePublication(under_test, kHostFullName, kServiceName, kInstanceName, kPort, kText,
                     sender_address0);
  subscriber.ExpectNoOther();

  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), 1, Media::kWireless, IpVersions::kV4);
  ReceivePublication(under_test, kHostFullName, kServiceName, kInstanceName, kPort, kText,
                     sender_address1);

  auto params = subscriber.ExpectInstanceDiscoveredCalled();
  EXPECT_EQ(
      ServiceInstance(kServiceName, kInstanceName, kHostName,
                      {inet::SocketAddress(sender_address1.socket_address().address(), kPort)},
                      kText, 0, 0),
      *params);

  subscriber.ExpectNoOther();
}

// Tests the behavior of the requestor when configured for wired-only operation.
TEST_F(InstanceRequestorTest, WiredOnly) {
  InstanceRequestor under_test(this, kServiceName, Media::kWired, IpVersions::kBoth, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);

  under_test.Start(kLocalHostFullName);

  // Expect a PTR question on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kWired, IpVersions::kBoth));
  ExpectQuestion(message.get(), kServiceFullName, DnsType::kPtr);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectPostTaskForTime(kMinDelay, kMinDelay);
  ExpectNoOther();

  subscriber.ExpectQueryCalled(DnsType::kPtr);
  subscriber.ExpectNoOther();

  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), 1, Media::kWireless, IpVersions::kV4);
  ReceivePublication(under_test, kHostFullName, kServiceName, kInstanceName, kPort, kText,
                     sender_address0);
  subscriber.ExpectNoOther();

  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), 1, Media::kWired, IpVersions::kV4);
  ReceivePublication(under_test, kHostFullName, kServiceName, kInstanceName, kPort, kText,
                     sender_address1);

  auto params = subscriber.ExpectInstanceDiscoveredCalled();
  EXPECT_EQ(
      ServiceInstance(kServiceName, kInstanceName, kHostName,
                      {inet::SocketAddress(sender_address1.socket_address().address(), kPort)},
                      kText, 0, 0),
      *params);

  subscriber.ExpectNoOther();
}

// Tests the behavior of the requestor when configured for IPv4-only operation.
TEST_F(InstanceRequestorTest, V4Only) {
  InstanceRequestor under_test(this, kServiceName, Media::kBoth, IpVersions::kV4, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);

  under_test.Start(kLocalHostFullName);

  // Expect a PTR question on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kV4));
  ExpectQuestion(message.get(), kServiceFullName, DnsType::kPtr);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectPostTaskForTime(kMinDelay, kMinDelay);
  ExpectNoOther();

  subscriber.ExpectQueryCalled(DnsType::kPtr);
  subscriber.ExpectNoOther();

  ReplyAddress sender_address0(inet::SocketAddress(0xfe80, 1, inet::IpPort::From_uint16_t(5353)),
                               inet::IpAddress(0xfe80, 100), 1, Media::kWired, IpVersions::kV6);
  ReceivePublication(under_test, kHostFullName, kServiceName, kInstanceName, kPort, kText,
                     sender_address0);
  subscriber.ExpectNoOther();

  ReplyAddress sender_address1(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), 1, Media::kWireless, IpVersions::kV4);
  ReceivePublication(under_test, kHostFullName, kServiceName, kInstanceName, kPort, kText,
                     sender_address1);

  auto params = subscriber.ExpectInstanceDiscoveredCalled();
  EXPECT_EQ(
      ServiceInstance(kServiceName, kInstanceName, kHostName,
                      {inet::SocketAddress(sender_address1.socket_address().address(), kPort)},
                      kText, 0, 0),
      *params);

  subscriber.ExpectNoOther();
}

// Tests the behavior of the requestor when configured for IPv6-only operation.
TEST_F(InstanceRequestorTest, V6Only) {
  InstanceRequestor under_test(this, kServiceName, Media::kBoth, IpVersions::kV6, kExcludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);

  under_test.Start(kLocalHostFullName);

  // Expect a PTR question on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kV6));
  ExpectQuestion(message.get(), kServiceFullName, DnsType::kPtr);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectPostTaskForTime(kMinDelay, kMinDelay);
  ExpectNoOther();

  subscriber.ExpectQueryCalled(DnsType::kPtr);
  subscriber.ExpectNoOther();

  ReplyAddress sender_address0(
      inet::SocketAddress(192, 168, 1, 1, inet::IpPort::From_uint16_t(5353)),
      inet::IpAddress(192, 168, 1, 100), 1, Media::kWired, IpVersions::kV4);
  ReceivePublication(under_test, kHostFullName, kServiceName, kInstanceName, kPort, kText,
                     sender_address0);
  subscriber.ExpectNoOther();

  ReplyAddress sender_address1(inet::SocketAddress(0xfe80, 1, inet::IpPort::From_uint16_t(5353)),
                               inet::IpAddress(0xfe80, 100), 1, Media::kWireless, IpVersions::kV6);
  ReceivePublication(under_test, kHostFullName, kServiceName, kInstanceName, kPort, kText,
                     sender_address1);

  auto params = subscriber.ExpectInstanceDiscoveredCalled();
  EXPECT_EQ(
      ServiceInstance(kServiceName, kInstanceName, kHostName,
                      {inet::SocketAddress(sender_address1.socket_address().address(), kPort, 1)},
                      kText, 0, 0),
      *params);

  subscriber.ExpectNoOther();
}

// Tests the behavior of the requestor when configured to discover services on the local host.
TEST_F(InstanceRequestorTest, LocalInstance) {
  InstanceRequestor under_test(this, kServiceName, Media::kBoth, IpVersions::kBoth, kIncludeLocal,
                               kExcludeLocalProxies);
  SetAgent(under_test);
  SetLocalHostAddresses(kHostAddresses);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);

  under_test.Start(kLocalHostFullName);

  // Expect a PTR question on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kServiceFullName, DnsType::kPtr);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectPostTaskForTime(kMinDelay, kMinDelay);
  ExpectNoOther();

  subscriber.ExpectQueryCalled(DnsType::kPtr);
  subscriber.ExpectNoOther();

  // Expect to see an added local service instance.
  under_test.OnAddLocalServiceInstance(
      ServiceInstance(kServiceName, kInstanceName, kLocalHostName, kSocketAddresses, kText),
      kFromLocalHost);

  auto params = subscriber.ExpectInstanceDiscoveredCalled();
  EXPECT_EQ(
      ServiceInstance(kServiceName, kInstanceName, kLocalHostName, kSocketAddressesReversed, kText),
      *params);

  // |OnChangeLocalServiceInstance| should do nothing if the instance doesn't change.
  under_test.OnChangeLocalServiceInstance(
      ServiceInstance(kServiceName, kInstanceName, kLocalHostName, kSocketAddresses, kText),
      kFromLocalHost);
  subscriber.ExpectNoOther();

  // If the instance does change, we should see the notification.
  under_test.OnChangeLocalServiceInstance(
      ServiceInstance(kServiceName, kInstanceName, kLocalHostName, kSocketAddresses, kAltText),
      false);
  params = subscriber.ExpectInstanceChangedCalled();
  EXPECT_EQ(ServiceInstance(kServiceName, kInstanceName, kLocalHostName, kSocketAddressesReversed,
                            kAltText),
            *params);
  subscriber.ExpectNoOther();

  // |OnRemoveLocalServiceInstance| should notify of a removal.
  under_test.OnRemoveLocalServiceInstance(kServiceName, kInstanceName, kFromLocalHost);
  auto lost_params = subscriber.ExpectInstanceLostCalled();
  EXPECT_EQ(kServiceName, lost_params->service_);
  EXPECT_EQ(kInstanceName, lost_params->instance_);

  // Expect that local proxy host instances are ignored.
  under_test.OnAddLocalServiceInstance(
      ServiceInstance(kServiceName, kInstanceName, kHostName, kSocketAddresses, kText),
      kFromLocalProxyHost);
  subscriber.ExpectNoOther();
}

// Tests the behavior of the requestor when configured to discover services on a local proxy host.
TEST_F(InstanceRequestorTest, LocalProxyInstance) {
  InstanceRequestor under_test(this, kServiceName, Media::kBoth, IpVersions::kBoth, kExcludeLocal,
                               kIncludeLocalProxies);
  SetAgent(under_test);
  SetLocalHostAddresses(kHostAddresses);

  Subscriber subscriber;
  under_test.AddSubscriber(&subscriber);

  under_test.Start(kLocalHostFullName);

  // Expect a PTR question on start.
  auto message = ExpectOutboundMessage(ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  ExpectQuestion(message.get(), kServiceFullName, DnsType::kPtr);
  ExpectNoOtherQuestionOrResource(message.get());
  ExpectPostTaskForTime(kMinDelay, kMinDelay);
  ExpectNoOther();

  subscriber.ExpectQueryCalled(DnsType::kPtr);
  subscriber.ExpectNoOther();

  // Expect to see an added local service instance.
  under_test.OnAddLocalServiceInstance(
      ServiceInstance(kServiceName, kInstanceName, kHostName, kSocketAddresses, kText),
      kFromLocalProxyHost);

  auto params = subscriber.ExpectInstanceDiscoveredCalled();
  EXPECT_EQ(
      ServiceInstance(kServiceName, kInstanceName, kHostName, kSocketAddressesReversed, kText),
      *params);

  // |OnChangeLocalServiceInstance| should do nothing if the instance doesn't change.
  under_test.OnChangeLocalServiceInstance(
      ServiceInstance(kServiceName, kInstanceName, kHostName, kSocketAddresses, kText),
      kFromLocalProxyHost);
  subscriber.ExpectNoOther();

  // If the instance does change, we should see the notification.
  under_test.OnChangeLocalServiceInstance(
      ServiceInstance(kServiceName, kInstanceName, kHostName, kSocketAddresses, kAltText),
      kFromLocalProxyHost);
  params = subscriber.ExpectInstanceChangedCalled();
  EXPECT_EQ(
      ServiceInstance(kServiceName, kInstanceName, kHostName, kSocketAddressesReversed, kAltText),
      *params);
  subscriber.ExpectNoOther();

  // |OnRemoveLocalServiceInstance| should notify of a removal.
  under_test.OnRemoveLocalServiceInstance(kServiceName, kInstanceName, kFromLocalProxyHost);
  auto lost_params = subscriber.ExpectInstanceLostCalled();
  EXPECT_EQ(kServiceName, lost_params->service_);
  EXPECT_EQ(kInstanceName, lost_params->instance_);

  // Expect that local host instances are ignored.
  under_test.OnAddLocalServiceInstance(
      ServiceInstance(kServiceName, kInstanceName, kLocalHostName, kSocketAddresses, kText),
      kFromLocalHost);
  subscriber.ExpectNoOther();
}

}  // namespace test
}  // namespace mdns
