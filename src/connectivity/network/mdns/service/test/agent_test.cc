// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/test/agent_test.h"

#include <iostream>

#include "src/connectivity/network/mdns/service/dns_formatting.h"

namespace mdns {

#ifndef MDNS_TRACE

std::ostream& operator<<(std::ostream& os, DnsType value) {
  switch (value) {
    case DnsType::kA:
      return os << "A";
    case DnsType::kNs:
      return os << "NS";
    case DnsType::kCName:
      return os << "CNAME";
    case DnsType::kPtr:
      return os << "PTR";
    case DnsType::kTxt:
      return os << "TXT";
    case DnsType::kAaaa:
      return os << "AAAA";
    case DnsType::kSrv:
      return os << "SRV";
    case DnsType::kOpt:
      return os << "OPT";
    case DnsType::kNSec:
      return os << "NSEC";
    case DnsType::kAny:
      return os << "any";
    default:
      return os << "TYPE " << static_cast<uint16_t>(value);
  }
}

#endif

std::ostream& operator<<(std::ostream& os, MdnsResourceSection value) {
  switch (value) {
    case MdnsResourceSection::kAnswer:
      return os << "answer";
    case MdnsResourceSection::kAuthority:
      return os << "authority";
    case MdnsResourceSection::kAdditional:
      return os << "additional";
    case MdnsResourceSection::kExpired:
      return os << "EXPIRED";
  }
}

namespace test {

const std::string AgentTest::kHostFullName = "testhost.local.";

void AgentTest::PostTaskForTime(MdnsAgent* agent, fit::closure task, zx::time target_time) {
  EXPECT_EQ(agent_, agent);
  post_task_for_time_calls_.push(
      PostTaskForTimeCall{.task_ = std::move(task), .target_time_ = target_time});
}

void AgentTest::SendQuestion(std::shared_ptr<DnsQuestion> question) {
  EXPECT_NE(nullptr, question);
  auto& message = outbound_messages_by_reply_address_[addresses_.multicast_reply()];
  if (message == nullptr) {
    message = std::make_unique<DnsMessage>();
  }
  message->questions_.push_back(question);
}

void AgentTest::SendResource(std::shared_ptr<DnsResource> resource, MdnsResourceSection section,
                             const ReplyAddress& reply_address) {
  EXPECT_NE(nullptr, resource);

  auto& message = outbound_messages_by_reply_address_[reply_address];
  if (message == nullptr) {
    message = std::make_unique<DnsMessage>();
  }

  switch (section) {
    case MdnsResourceSection::kAnswer:
      message->answers_.push_back(resource);
      break;
    case MdnsResourceSection::kAuthority:
      message->authorities_.push_back(resource);
      break;
    case MdnsResourceSection::kAdditional:
      message->additionals_.push_back(resource);
      break;
    case MdnsResourceSection::kExpired:
      EXPECT_TRUE(false);
      break;
  }
}

void AgentTest::SendAddresses(MdnsResourceSection section, const ReplyAddress& reply_address) {
  SendResource(address_placeholder_, section, reply_address);
}

void AgentTest::Renew(const DnsResource& resource) {
  renew_calls_.push(RenewCall{.resource_ = resource});
}

void AgentTest::RemoveAgent(std::shared_ptr<MdnsAgent> agent) {
  EXPECT_EQ(agent_, agent.get());
  remove_agent_called_ = true;
}

void AgentTest::FlushSentItems() { flush_sent_items_called_ = true; }

void AgentTest::AdvanceTo(zx::time time) {
  EXPECT_LE(now_, time);
  now_ = time;
}

std::pair<fit::closure, zx::time> AgentTest::ExpectPostTaskForTime(zx::duration earliest,
                                                                   zx::duration latest) {
  EXPECT_FALSE(post_task_for_time_calls_.empty());
  auto task = std::move(post_task_for_time_calls_.front().task_);
  EXPECT_NE(nullptr, task);
  auto time = post_task_for_time_calls_.front().target_time_;
  post_task_for_time_calls_.pop();
  EXPECT_LE(now_ + earliest, time);
  EXPECT_GE(now_ + latest, time);
  return std::pair(std::move(task), time);
}

void AgentTest::ExpectPostTaskForTimeAndInvoke(zx::duration earliest, zx::duration latest) {
  auto [task, time] = ExpectPostTaskForTime(earliest, latest);
  ExpectNoOther();

  AdvanceTo(time);
  task();
}

std::unique_ptr<DnsMessage> AgentTest::ExpectOutboundMessage(ReplyAddress reply_address) {
  auto message = std::move(outbound_messages_by_reply_address_[reply_address]);
  EXPECT_NE(nullptr, message);
  outbound_messages_by_reply_address_.erase(reply_address);
  return message;
}

void AgentTest::ExpectNoOther() {
  ExpectNoPostTaskForTime();
  ExpectNoOutboundMessage();
  ExpectNoRenewCalls();
  ExpectNoRemoveAgentCall();
}

void AgentTest::ExpectQuestion(DnsMessage* message, const std::string& name, DnsType type,
                               DnsClass dns_class, bool unicast_response) {
  EXPECT_NE(nullptr, message);
  for (auto i = message->questions_.begin(); i != message->questions_.end(); ++i) {
    if ((*i)->name_.dotted_string_.compare(name) == 0 && (*i)->type_ == type &&
        (*i)->class_ == dns_class && (*i)->unicast_response_ == unicast_response) {
      message->questions_.erase(i);
      return;
    }
  }

  EXPECT_TRUE(false) << "No matching question with name " << name << " and type " << type
                     << " in message.";
}

std::shared_ptr<DnsResource> AgentTest::ExpectResource(DnsMessage* message,
                                                       MdnsResourceSection section,
                                                       const std::string& name, DnsType type,
                                                       DnsClass dns_class, bool cache_flush) {
  EXPECT_NE(nullptr, message);

  std::vector<std::shared_ptr<DnsResource>>* collection;
  switch (section) {
    case MdnsResourceSection::kAnswer:
      collection = &message->answers_;
      break;
    case MdnsResourceSection::kAuthority:
      collection = &message->authorities_;
      break;
    case MdnsResourceSection::kAdditional:
      collection = &message->additionals_;
      break;
    case MdnsResourceSection::kExpired:
      EXPECT_TRUE(false);
      return nullptr;
  }

  for (auto i = collection->begin(); i != collection->end(); ++i) {
    if ((*i)->name_.dotted_string_.compare(name) == 0 && (*i)->type_ == type &&
        (*i)->class_ == dns_class && (*i)->cache_flush_ == cache_flush) {
      auto result = std::move(*i);
      collection->erase(i);
      return result;
    }
  }

  EXPECT_TRUE(false) << "No matching resource with name " << name << " and type " << type
                     << " in section " << section << " of message.";
  return nullptr;
}

void AgentTest::ExpectAddressPlaceholder(DnsMessage* message, MdnsResourceSection section) {
  ExpectResource(message, section, kHostFullName, DnsType::kA);
}

void AgentTest::ExpectNoOtherQuestionOrResource(DnsMessage* message) {
  EXPECT_NE(nullptr, message);
  EXPECT_TRUE(message->questions_.empty());
  EXPECT_TRUE(message->answers_.empty());
  EXPECT_TRUE(message->authorities_.empty());
  EXPECT_TRUE(message->additionals_.empty());
}

}  // namespace test
}  // namespace mdns
