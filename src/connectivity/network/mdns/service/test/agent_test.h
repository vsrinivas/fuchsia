// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_TEST_AGENT_TEST_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_TEST_AGENT_TEST_H_

#include <lib/zx/time.h>

#include <queue>
#include <unordered_map>

#include <gtest/gtest.h>

#include "src/connectivity/network/mdns/service/agents/mdns_agent.h"

namespace mdns {
namespace test {

class AgentTest : public ::testing::Test, public MdnsAgent::Owner {
 public:
  AgentTest() {}

 protected:
  static constexpr zx::time kInitialTime = zx::time(1000);
  static const std::string kLocalHostName;
  static const std::string kLocalHostFullName;
  static const std::string kAlternateCaseLocalHostFullName;

  // Sets the agent under test. This must be called before the test gets underway, and the agent
  // must survive until the end of the test.
  void SetAgent(const MdnsAgent& agent) { agent_ = &agent; }

  // Sets the host addresses returned by LocalHostAddresses.
  void SetLocalHostAddresses(std::vector<HostAddress> local_host_addresses) {
    local_host_addresses_ = std::move(local_host_addresses);
  }

  // Advances the current time (as returned by |now()|) to |time|. |time| must be greater than
  // or equal to the time currently returned by |now()|.
  void AdvanceTo(zx::time time);

  // Expects that the agent hasn't posted any new tasks.
  void ExpectNoPostTaskForTime() { EXPECT_TRUE(post_task_for_time_calls_.empty()); }

  // Expects that the agent has posted a task for a time in the given range. Returns the task
  // closure and the actual scheduled time.
  std::pair<fit::closure, zx::time> ExpectPostTaskForTime(zx::duration earliest,
                                                          zx::duration latest);
  // Calls |ExpectPostTaskForTime|, advances the time to the scheduled time of the task, and
  // invokes the task.
  void ExpectPostTaskForTimeAndInvoke(zx::duration earliest, zx::duration latest);

  // Expects that there is no outbond message.
  void ExpectNoOutboundMessage() { EXPECT_TRUE(outbound_messages_by_reply_address_.empty()); }

  // Expects that there is an outbound message targeted at |reply_address| and returns it.
  std::unique_ptr<DnsMessage> ExpectOutboundMessage(ReplyAddress reply_address);

  // Expects that the agent has not called |Renew|.
  void ExpectNoRenewCalls() { EXPECT_TRUE(renew_calls_.empty()); }

  // Expects that the agent has asked |resource| to be renewed.
  void ExpectRenewCall(DnsResource resource);

  // Expects that the agent has not asked for any resources to be expired.
  void ExpectNoExpirations() { EXPECT_TRUE(expirations_.empty()); }

  // Expects that the agent has asked |resource| to be expired.
  void ExpectExpiration(DnsResource resource);

  // Expects that the agent has not called |RemoveAgent|.
  void ExpectNoRemoveAgentCall() { EXPECT_FALSE(remove_agent_called_); }

  // Expects that the agent has called |RemoveAgent| to remove itself.
  void ExpectRemoveAgentCall() {
    EXPECT_TRUE(remove_agent_called_);
    remove_agent_called_ = false;
  }

  // Expects that the agent has not called |FlushSentItems|.
  void ExpectNoFlushSentItemsCall() { EXPECT_FALSE(flush_sent_items_called_); }

  // Expects that the agent has called |FlushSentItems|.
  void ExpectFlushSentItemsCall() {
    EXPECT_TRUE(flush_sent_items_called_);
    flush_sent_items_called_ = false;
  }

  // Expects that the agent has not called |AddLocalServiceInstance|.
  void ExpectNoAddLocalServiceInstanceCall() const {
    EXPECT_FALSE(add_local_service_instance_called_);
  }

  // Expects that the agent has called |AddLocalServiceInstance|.
  void ExpectAddLocalServiceInstanceCall(const ServiceInstance& instance, bool from_proxy) {
    EXPECT_TRUE(add_local_service_instance_called_);
    EXPECT_EQ(instance, add_local_service_instance_instance_);
    EXPECT_EQ(from_proxy, add_local_service_instance_from_proxy_);
    add_local_service_instance_called_ = false;
  }

  // Expects that the agent has not called |ChangeLocalServiceInstance|.
  void ExpectNoChangeLocalServiceInstanceCall() const {
    EXPECT_FALSE(change_local_service_instance_called_);
  }

  // Expects that the agent has called |ChangeLocalServiceInstance|.
  void ExpectChangeLocalServiceInstanceCall(const ServiceInstance& instance, bool from_proxy) {
    EXPECT_TRUE(change_local_service_instance_called_);
    EXPECT_EQ(instance, change_local_service_instance_instance_);
    EXPECT_EQ(from_proxy, change_local_service_instance_from_proxy_);
    change_local_service_instance_called_ = false;
  }

  // Expects that nothing else has happened. Subclasses can override this to ensure that nothing
  // specific to a particular agent type has happened. Overrides should call this implementation.
  virtual void ExpectNoOther();

  // Expects that |message| contains a question with the given parameters.
  void ExpectQuestion(DnsMessage* message, const std::string& name, DnsType type,
                      DnsClass dns_class = DnsClass::kIn, bool unicast_response = false);

  // Expects that |message| contains a resource in |section| with the given parameters and returns
  // it.
  std::shared_ptr<DnsResource> ExpectResource(DnsMessage* message, MdnsResourceSection section,
                                              const std::string& name, DnsType type,
                                              DnsClass dns_class = DnsClass::kIn,
                                              bool cache_flush = true);

  // Expects that |message| contains one or more resources in |section| with the given parameters
  // and returns them.
  std::vector<std::shared_ptr<DnsResource>> ExpectResources(DnsMessage* message,
                                                            MdnsResourceSection section,
                                                            const std::string& name, DnsType type,
                                                            DnsClass dns_class = DnsClass::kIn,
                                                            bool cache_flush = true);

  // Expects that |message| contains an address placeholder resource in |section|.
  void ExpectAddressPlaceholder(DnsMessage* message, MdnsResourceSection section);

  // Expects that |message| contains resources for |addresses| in |section|.
  void ExpectAddresses(DnsMessage* message, MdnsResourceSection section,
                       const std::string& host_full_name,
                       const std::vector<inet::IpAddress>& addresses);

  // Expect that |address| appears in |resources| and remove it.
  void ExpectAddress(std::vector<std::shared_ptr<DnsResource>>& resources, inet::IpAddress address);

  // Expects that |message| contains no questions or resources.
  void ExpectNoOtherQuestionOrResource(DnsMessage* message);

 private:
  struct PostTaskForTimeCall {
    fit::closure task_;
    zx::time target_time_;
  };

  struct RenewCall {
    DnsResource resource_;
  };

  struct ReplyAddressHash {
    std::size_t operator()(const ReplyAddress& reply_address) const noexcept {
      return std::hash<inet::SocketAddress>{}(reply_address.socket_address()) ^
             (std::hash<inet::IpAddress>{}(reply_address.interface_address()) << 1) ^
             (std::hash<uint32_t>{}(reply_address.interface_id()) << 2) ^
             (std::hash<Media>{}(reply_address.media()) << 3) ^
             (std::hash<IpVersions>{}(reply_address.ip_versions()) << 4);
    }
  };

  // |MdnsAgent::Owner| implementation.
 protected:
  zx::time now() override { return now_; }

 private:
  void PostTaskForTime(MdnsAgent* agent, fit::closure task, zx::time target_time) override;

  void SendQuestion(std::shared_ptr<DnsQuestion> question, ReplyAddress reply_address) override;

  void SendResource(std::shared_ptr<DnsResource> resource, MdnsResourceSection section,
                    const ReplyAddress& reply_address) override;

  void SendAddresses(MdnsResourceSection section, const ReplyAddress& reply_address) override;

  void Renew(const DnsResource& resource, Media media, IpVersions ip_versions) override;

  void RemoveAgent(std::shared_ptr<MdnsAgent> agent) override;

  void FlushSentItems() override;

  void AddLocalServiceInstance(const ServiceInstance& instance, bool from_proxy) override;

  void ChangeLocalServiceInstance(const ServiceInstance& instance, bool from_proxy) override;

  std::vector<HostAddress> LocalHostAddresses() override { return local_host_addresses_; }

  const MdnsAgent* agent_;
  std::vector<HostAddress> local_host_addresses_;
  std::shared_ptr<DnsResource> address_placeholder_ =
      std::make_shared<DnsResource>(kLocalHostFullName, DnsType::kA);

  zx::time now_ = kInitialTime;

  std::queue<PostTaskForTimeCall> post_task_for_time_calls_;
  std::unordered_map<ReplyAddress, std::unique_ptr<DnsMessage>, ReplyAddressHash>
      outbound_messages_by_reply_address_;
  std::vector<DnsResource> renew_calls_;
  std::vector<DnsResource> expirations_;
  bool remove_agent_called_ = false;
  bool flush_sent_items_called_ = false;
  bool add_local_service_instance_called_ = false;
  ServiceInstance add_local_service_instance_instance_;
  bool add_local_service_instance_from_proxy_;
  bool change_local_service_instance_called_ = false;
  ServiceInstance change_local_service_instance_instance_;
  bool change_local_service_instance_from_proxy_;
};

}  // namespace test
}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_TEST_AGENT_TEST_H_
