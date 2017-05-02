// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "apps/maxwell/src/context_engine/context_repository.h"

#include "apps/maxwell/lib/context/formatting.h"
#include "apps/maxwell/src/context_engine/scope_utils.h"

namespace maxwell {

namespace {

bool QueryMatches(const std::set<std::string>& updated_topics,
                  const ContextQueryPtr& query) {
  // The wildcard query is one without any topics.
  if (query->topics.size() == 0)
    return true;

  // Otherwise any of |updated_topics| must appear in |query->topics|.
  for (const auto& query_topic : query->topics) {
    if (updated_topics.count(query_topic) > 0)
      return true;
  }
  return false;
}

}  // namespace

ContextRepository::ContextRepository() = default;
ContextRepository::~ContextRepository() = default;

void ContextRepository::Set(const std::string& topic,
                            const std::string& json_value) {
  SetInternal(topic, &json_value);
}

const std::string* ContextRepository::Get(const std::string& topic) const {
  auto it = values_.find(topic);
  if (it == values_.end())
    return nullptr;
  return &it->second;
}

void ContextRepository::Remove(const std::string& topic) {
  SetInternal(topic, nullptr);
}

ContextRepository::SubscriptionId ContextRepository::AddSubscription(
    ContextQueryPtr query,
    ContextListener* listener) {
  // If we already have a value for any topics in |query|, notify the
  // listener immediately.
  auto result = BuildContextUpdate(query);
  if (result) {
    FTL_DCHECK(!result->values.is_null());
    listener->OnUpdate(std::move(result));
  }

  // Add the subscription to our list.
  Subscription subscription{std::move(query), std::move(listener)};
  return subscriptions_.emplace(subscriptions_.begin(),
                                std::move(subscription));
}

void ContextRepository::RemoveSubscription(SubscriptionId id) {
  subscriptions_.erase(id);
}

void ContextRepository::AddCoprocessor(ContextCoprocessor* coprocessor) {
  coprocessors_.emplace_back(coprocessor);
}

void ContextRepository::GetAllValuesInStoryScope(
    const std::string& story_id,
    const std::string& topic,
    std::vector<std::string>* output) const {
  FTL_DCHECK(output != nullptr);
  // TODO(thatguy): This is currently O(n), where n = the number of topics in
  // the repository.  To say the least, this is low-hanging fruit for
  // optimization. We can store one or more secondary indexes to context
  // values, taking into account the various access patterns once we have a
  // better idea of what those are.
  for (const auto& entry : values_) {
    const auto& entry_topic = entry.first;
    std::string entry_story_id;
    std::string entry_module_id;
    std::string entry_local_topic;
    if (!ParseModuleScopeTopic(entry_topic, &entry_story_id, &entry_module_id,
                               &entry_local_topic)) {
      continue;
    }

    if (entry_story_id == story_id && entry_local_topic == topic) {
      output->push_back(entry.second);
    }
  }
}

void ContextRepository::SetInternal(const std::string& topic,
                                    const std::string* json_value) {
  if (json_value != nullptr) {
    FTL_LOG(INFO) << "ContextRepository::SetInternal(): " << topic << " = "
                  << *json_value;
    values_[topic] = *json_value;
  } else {
    FTL_LOG(INFO) << "ContextRepository::SetInternal(): " << topic << " = null";
    values_.erase(topic);
  }

  // Run all the coprocessors we have registered.
  std::set<std::string> topics_updated{topic};
  for (auto& coprocessor : coprocessors_) {
    std::map<std::string, std::string> new_updates;
    coprocessor->ProcessTopicUpdate(this, topics_updated, &new_updates);
    // Apply any new updates we are instructed to do.
    for (const auto& entry : new_updates) {
      topics_updated.insert(entry.first);
      values_[entry.first] = entry.second;
    }
  }

  // Find any queries matching this updated topic and notify their respective
  // listeners.
  //
  // TODO(thatguy): This evaluation won't scale. Change it when it becomes a
  // bottleneck.
  for (const auto& subscription : subscriptions_) {
    if (QueryMatches(topics_updated, subscription.query)) {
      auto result = BuildContextUpdate(subscription.query);
      FTL_DCHECK(!result->values.is_null());
      subscription.listener->OnUpdate(std::move(result));
    }
  }
}

ContextUpdatePtr ContextRepository::BuildContextUpdate(
    const ContextQueryPtr& query) {
  ContextUpdatePtr result;  // Null by default.

  if (query->topics.size() == 0) {
    // Wildcard query. Send everything.
    for (const auto& entry : values_) {
      if (!result)
        result = ContextUpdate::New();
      result->values[entry.first] = entry.second;
    }
  } else {
    for (const auto& topic : query->topics) {
      auto it = values_.find(topic);
      if (it != values_.end()) {
        if (!result)
          result = ContextUpdate::New();

        result->values[topic] = it->second;
      }
    }
  }

  FTL_DCHECK(!result || !result->values.is_null());

  return result;
}

/*
 * ContextCoprocessor
 */
ContextCoprocessor::~ContextCoprocessor() = default;

}  // namespace maxwell
