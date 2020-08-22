// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_TEST_AGENT_TEST_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_TEST_AGENT_TEST_H_

#include <lib/zx/time.h>

#include <queue>
#include <unordered_map>

#include <gtest/gtest.h>

#include "src/connectivity/network/mdns/service/mdns_agent.h"

namespace mdns {
namespace test {

class AgentTest : public ::testing::Test, public MdnsAgent::Host {
 public:
  AgentTest() {}

 protected:
  static constexpr zx::time kInitialTime = zx::time(1000);
  static const std::string kHostFullName;

  // Sets the agent under test. This must be called before the test gets underway, and the agent
  // must survive until the end of the test.
  void SetAgent(const MdnsAgent& agent) { agent_ = &agent; }

  // An instance of |MdnsAddresses| for lookup.
  const MdnsAddresses& addresses() const { return addresses_; }

  // Advances the current time (as returned by |now()|) to |time|. |time| must be greater than
  // or equal to the time currently returned by |now()|.
  void AdvanceTo(zx::time time);

  // Expects that the agent hasn't posted any new tasks.
  void ExpectNoPostTaskForTime() { EXPECT_TRUE(post_task_for_time_calls_.empty()); }

  // Expects that the agent has posted a task for a time in the given range. Returns the task
  // closure and the actual scheduled time.
  std::pair<fit::closure, zx::time> ExpectPostTaskForTime(zx::duration earliest,
                                                          zx::duration latest);
  // Calls |ExpectNoPostTaskForTime|, advances the time to the scheduled time of the task, and
  // invokes the task.
  void ExpectPostTaskForTimeAndInvoke(zx::duration earliest, zx::duration latest);

  // Expects that there is no outbond message.
  void ExpectNoOutboundMessage() { EXPECT_TRUE(outbound_messages_by_reply_address_.empty()); }

  // Expects that there is an outbound message targeted at |reply_address| and returns it.
  std::unique_ptr<DnsMessage> ExpectOutboundMessage(ReplyAddress reply_address);

  // Expects that the agent has not called |Renew|.
  void ExpectNoRenewCalls() { EXPECT_TRUE(renew_calls_.empty()); }

  // Expects that the agent has not called |RemoveAgent|.
  void ExpectNoRemoveAgentCall() { EXPECT_FALSE(remove_agent_called_); }

  // Expects that the agent has called |RemoveAgent| to remove itself.
  void ExpectRemoveAgentCall() { EXPECT_TRUE(remove_agent_called_); }

  // Expects that the agent has not called |FlushSentItems|.
  void ExpectNoFlushSentItemsCall() { EXPECT_FALSE(flush_sent_items_called_); }

  // Expects that the agent has called |FlushSentItems|.
  void ExpectFlushSentItemsCall() { EXPECT_TRUE(flush_sent_items_called_); }

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

  // Expects that |message| contains an address placeholder resource in |section|.
  void ExpectAddressPlaceholder(DnsMessage* message, MdnsResourceSection section);

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
             (std::hash<Media>{}(reply_address.media()) << 2);
    }
  };

  // |MdnsAgent::Host| implementation.
 protected:
  zx::time now() override { return now_; }

 private:
  void PostTaskForTime(MdnsAgent* agent, fit::closure task, zx::time target_time) override;

  void SendQuestion(std::shared_ptr<DnsQuestion> question) override;

  void SendResource(std::shared_ptr<DnsResource> resource, MdnsResourceSection section,
                    const ReplyAddress& reply_address) override;

  void SendAddresses(MdnsResourceSection section, const ReplyAddress& reply_address) override;

  void Renew(const DnsResource& resource) override;

  void RemoveAgent(std::shared_ptr<MdnsAgent> agent) override;

  void FlushSentItems() override;

  const MdnsAgent* agent_;
  std::shared_ptr<DnsResource> address_placeholder_ =
      std::make_shared<DnsResource>(kHostFullName, DnsType::kA);

  MdnsAddresses addresses_;
  zx::time now_ = kInitialTime;

  std::queue<PostTaskForTimeCall> post_task_for_time_calls_;
  std::unordered_map<ReplyAddress, std::unique_ptr<DnsMessage>, ReplyAddressHash>
      outbound_messages_by_reply_address_;
  std::queue<RenewCall> renew_calls_;
  bool remove_agent_called_ = false;
  bool flush_sent_items_called_ = false;
};

}  // namespace test
}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_TEST_AGENT_TEST_H_
