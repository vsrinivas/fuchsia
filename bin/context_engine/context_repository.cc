// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "apps/maxwell/src/context_engine/context_repository.h"

#include "apps/maxwell/lib/context/formatting.h"
#include "apps/maxwell/src/context_engine/scope_utils.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "rapidjson/document.h"

namespace maxwell {

namespace {

// FilterFunctions compare a topic |value| to a given value in |filter|.
// |*output_value| is set to the value that should be sent to listeners of the
// query which contained |filter|. If |*output_value| is a null
// rapidjson::Value, the |value| is sent to clients instead.
//
// FilterFunctions are applied independently to topics during ContextQuery
// evaluation.
using FilterFunction = std::function<bool(const ContextFilterPtr& filter,
                                          const rapidjson::Value& value,
                                          rapidjson::Document* output_value)>;

bool EvaluateFilter(const ContextFilterPtr& filter,
                    const rapidjson::Value& value,
                    rapidjson::Document* output_value);

// Recursive evaluator for partial equality.
bool PartialEq(const rapidjson::Value& value,
               const rapidjson::Value& filter_value) {
  if (value.GetType() != filter_value.GetType()) {
    return false;
  }

  if (filter_value.IsObject()) {
    for (auto it = filter_value.MemberBegin(); it != filter_value.MemberEnd();
         ++it) {
      if (!value.HasMember(it->name)) {
        return false;
      }

      if (!PartialEq(value[it->name], it->value)) {
        return false;
      }
    }

    return true;
  }

  return value == filter_value;
}

// |partial_eq| filter implementation.
bool Filter_PartialEq(const ContextFilterPtr& filter,
                      const rapidjson::Value& value,
                      rapidjson::Document* output_value) {
  rapidjson::Document filter_value;
  filter_value.Parse(filter->get_partial_eq());
  if (filter_value.HasParseError()) {
    FTL_LOG(WARNING) << "Invalid partial_eq filter JSON: "
                     << filter->get_partial_eq();
    return false;
  }

  return PartialEq(value, filter_value);
}

// |for_each| filter implementation.
bool Filter_ForEach(const ContextFilterPtr& filter,
                    const rapidjson::Value& value,
                    rapidjson::Document* output_value) {
  if (!value.IsArray())
    return false;

  output_value->SetArray();
  auto& allocator = output_value->GetAllocator();

  for (auto& v : value.GetArray()) {
    rapidjson::Document new_element;
    if (EvaluateFilter(filter->get_for_each(), v, &new_element)) {
      output_value->PushBack(rapidjson::Value(new_element, allocator),
                             allocator);
    }
  }

  return output_value->Size() > 0;
}

// Returns true if |filter| evaluates to true for |value| and |*output_value|
// will contain the value that should be returned to listeners.
bool EvaluateFilter(const ContextFilterPtr& filter,
                    const rapidjson::Value& value,
                    rapidjson::Document* output_value) {
  FTL_CHECK(output_value != nullptr);
  if (!filter) return false;

  FilterFunction func;
  if (filter->is_partial_eq()) {
    func = Filter_PartialEq;
  } else if (filter->is_for_each()) {
    func = Filter_ForEach;
  } else {
    FTL_LOG(FATAL) << "Unknown ContextFilter type.";
  }

  rapidjson::Document func_output;
  auto matches = func(filter, value, &func_output);
  if (matches) {
    if (!func_output.IsNull()) {
      output_value->Swap(func_output);
    } else {
      // By default, use the existing value.
      output_value->CopyFrom(value, output_value->GetAllocator());
    }
  }
  return matches;
}

}  // namespace

//// ContextValue
ContextValue::ContextValue() : meta(ContextMetadata::New()) {
  meta->value =
      fidl::Map<ContextType, fidl::Map<fidl::String, fidl::String>>();
  meta->value.mark_non_null();
}

//// ContextRepository
ContextRepository::ContextRepository() : next_subscription_id_(0) {}
ContextRepository::~ContextRepository() = default;

void ContextRepository::Set(const std::string& topic,
                            const std::string& json_value) {
  SetInternal(topic, &json_value);
}

const std::string* ContextRepository::Get(const std::string& topic) const {
  auto it = values_.find(topic);
  if (it == values_.end())
    return nullptr;
  return &it->second.value;
}

// TODO(thatguy): Remove() is only used in tests. Deprecate in favor of
// clients just setting the value to JSON null.
void ContextRepository::Remove(const std::string& topic) {
  SetInternal(topic, nullptr);
}

ContextRepository::SubscriptionId ContextRepository::AddSubscription(
    ContextQueryPtr query,
    ContextListener* listener) {
  // If the query already matches our current state, notify the listener
  // immediately.
  ContextUpdatePtr update;
  if (EvaluateQueryAndBuildUpdate(query, &update)) {
    FTL_DCHECK(!update->values.is_null());
    listener->OnUpdate(std::move(update));
  }

  // Add the subscription to our list.
  Subscription subscription{std::move(query), std::move(listener)};
  auto it =
      subscriptions_.emplace(next_subscription_id_++, std::move(subscription));
  FTL_DCHECK(it.second);
  const auto id = it.first->first;

  // Add |id| to our inverted index.
  const auto& query_again = it.first->second.query;
  if (query_again->topics.empty()) {
    // It's a wildcard query.
    wildcard_subscription_ids_.insert(id);
  } else {
    for (const auto& topic : query_again->topics) {
      topic_to_subscription_id_[topic].insert(id);
    }
  }
  return id;
}

void ContextRepository::RemoveSubscription(SubscriptionId id) {
  // Remove |id| from our inverted index.
  auto it = subscriptions_.find(id);
  FTL_DCHECK(it != subscriptions_.end());
  const auto& query = it->second.query;
  if (query->topics.empty()) {
    // It's a wildcard query.
    wildcard_subscription_ids_.erase(id);
  } else {
    for (const auto& topic : query->topics) {
      topic_to_subscription_id_[topic].erase(id);
    }
  }
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
      output->push_back(entry.second.value);
    }
  }
}

void ContextRepository::GetAllTopicsWithPrefix(
    const std::string& prefix,
    std::vector<std::string>* output) const {
  auto it = values_.lower_bound(prefix);
  // ~ is the last character in the ASCII table.
  auto end = values_.lower_bound(prefix + "~");

  for (; it != end; ++it) {
    output->push_back(it->first);
  }
}

void ContextRepository::SetInternal(const std::string& topic,
                                    const std::string* json_value) {
  FTL_DCHECK(topic.find('\'') == std::string::npos) << topic;
  if (json_value != nullptr) {
    FTL_VLOG(4) << "ContextRepository::SetInternal(): " << topic << "|"
                  << topic.length() << " = " << *json_value;
    values_[topic].value = *json_value;
  } else {
    FTL_VLOG(4) << "ContextRepository::SetInternal(): " << topic << " = null";
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
      values_[entry.first].value = entry.second;
    }
  }

  // Find any subscriptions with queries matching any updated topics.
  std::set<SubscriptionId> subscribed_ids;
  subscribed_ids.insert(wildcard_subscription_ids_.begin(),
                        wildcard_subscription_ids_.end());
  for (const auto& topic : topics_updated) {
    auto& subscriptions_for_topic = topic_to_subscription_id_[topic];
    subscribed_ids.insert(subscriptions_for_topic.begin(),
                          subscriptions_for_topic.end());
  }

  for (auto id : subscribed_ids) {
    auto it = subscriptions_.find(id);
    FTL_DCHECK(it != subscriptions_.end());
    const auto& subscription = it->second;
    ContextUpdatePtr update;
    if (EvaluateQueryAndBuildUpdate(subscription.query, &update)) {
      FTL_DCHECK(!update->values.is_null());
      subscription.listener->OnUpdate(std::move(update));
    }
  }
}

bool ContextRepository::EvaluateQueryAndBuildUpdate(
    const ContextQueryPtr& query,
    ContextUpdatePtr* update_output) {
  // Wildcard query? Send everything.
  if (query->topics.size() == 0) {
    for (const auto& entry : values_) {
      if (!(*update_output))
        *update_output = ContextUpdate::New();
      (*update_output)->values[entry.first] = entry.second.value;
      (*update_output)->meta[entry.first] = entry.second.meta.Clone();
    }
    return true;
  }

  for (const auto& topic : query->topics) {
    const std::string* current_value = Get(topic);
    if (current_value == nullptr) {
      // If a topic listed in the topics doesn't exist, return false.
      // TODO(thatguy): Revisit this. Instead, use the filters to gate updates.
      return false;
    }
    std::string value_to_send = *current_value;
    if (query->filters) {
      auto it = query->filters.find(topic);
      if (it != query->filters.end()) {
        const auto& filter = it.GetValue();

        rapidjson::Document json_value;
        json_value.Parse(*current_value);
        FTL_CHECK(!json_value.HasParseError());

        rapidjson::Document filtered_value;
        if (!EvaluateFilter(filter, json_value, &filtered_value)) {
          return false;
        }

        value_to_send = modular::JsonValueToString(filtered_value);
      }
    }

    if (!(*update_output))
      *update_output = ContextUpdate::New();
    (*update_output)->values[topic] = value_to_send;
    (*update_output)->meta[topic] = values_[topic].meta.Clone();
  }

  return true;
}

/*
 * ContextCoprocessor
 */
ContextCoprocessor::~ContextCoprocessor() = default;

}  // namespace maxwell
