// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/context_repository.h"

namespace maxwell {

void ContextRepository::Set(const std::string& topic, const std::string& json_value) {
  SetInternal(topic, &json_value);
}

void ContextRepository::Remove(const std::string& topic) {
  SetInternal(topic, nullptr);
}

void ContextRepository::SetInternal(const std::string& topic,
                       const std::string* json_value) {
  ContextUpdate update;
  // update.source = component_->url;

  if (json_value != nullptr) {
    values_[topic] = *json_value;
    update.json_value = *json_value;
  } else {
    values_.erase(topic);
  }

  // Notify any subscriptions watching this topic.
  auto it = subscriptions_.find(topic);
  if (it == subscriptions_.end())
    return;

  for (auto& subscriber_link : it->second) {
    subscriber_link->OnUpdate(update.Clone());
  }
}

void ContextRepository::AddSubscription(const std::string& topic,
                           ContextSubscriberLinkPtr subscriber) {
  // If we already have a value for |topic|, notify the subscriber immediately.
  auto it = values_.find(topic);
  if (it != values_.end()) {
    auto update = ContextUpdate::New();
    update->json_value = it->second;
    subscriber->OnUpdate(std::move(update));
  }

  subscriptions_[topic].emplace(std::move(subscriber));
}

}  // namespace maxwell
