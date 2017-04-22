// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/context_repository.h"

#include "apps/maxwell/lib/context/formatting.h"

namespace maxwell {

namespace {

bool QueryMatches(const std::string& updated_topic,
                  const ContextQueryPtr& query) {
  // The wildcard query is one without any topics.
  if (query->topics.size() == 0) return true;

  // Otherwise |updated_topic| must appear in |query->topics|.
  return query->topics.To<std::set<std::string>>().count(updated_topic) > 0;
}

}  // namespace

ContextRepository::ContextRepository() = default;
ContextRepository::~ContextRepository() = default;

void ContextRepository::Set(const std::string& topic,
                            const std::string& json_value) {
  SetInternal(topic, &json_value);
}

void ContextRepository::Remove(const std::string& topic) {
  SetInternal(topic, nullptr);
}

void ContextRepository::AddSubscription(ContextQueryPtr query,
                                        ContextListenerPtr listener) {
  // If we already have a value for any topics in |query|, notify the
  // listener immediately.
  auto result = BuildContextUpdate(query);
  if (result) {
    FTL_DCHECK(!result->values.is_null());
    listener->OnUpdate(std::move(result));
  }

  // Add the subscription to our list.
  Subscription subscription{std::move(query), std::move(listener)};
  auto it =
      subscriptions_.emplace(subscriptions_.begin(), std::move(subscription));
  it->listener.set_connection_error_handler([this, it] {
    // Remove this subscription when it has an error.
    subscriptions_.erase(it);
  });
}

void ContextRepository::SetInternal(const std::string& topic,
                                    const std::string* json_value) {
  if (json_value != nullptr) {
    values_[topic] = *json_value;
  } else {
    values_.erase(topic);
  }

  // Find any queries matching this updated topic and notify their respective
  // listeners.
  //
  // TODO(thatguy): This evaluation won't scale. Change it when it becomes a
  // bottleneck.
  for (const auto& subscription : subscriptions_) {
    if (QueryMatches(topic, subscription.query)) {
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
      if (!result) result = ContextUpdate::New();
      result->values[entry.first] = entry.second;
    }
  } else {
    for (const auto& topic : query->topics) {
      auto it = values_.find(topic);
      if (it != values_.end()) {
        if (!result) result = ContextUpdate::New();

        result->values[topic] = it->second;
      }
    }
  }

  FTL_DCHECK(!result || !result->values.is_null());

  return result;
}

}  // namespace maxwell
