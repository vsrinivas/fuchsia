// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <unordered_map>

#include "apps/maxwell/services/context/context_provider.fidl.h"

namespace maxwell {

// Tracks current values of context topics as well as subscriptions to those
// topics. Is responsible for notifying any subscribed clients whenever a topic
// changes value.
class ContextRepository {
  struct Subscription;

 public:
  using SubscriptionId = std::list<Subscription>::const_iterator;

  ContextRepository();
  ~ContextRepository();

  void Set(const std::string& topic, const std::string& json_value);
  // Returns nullptr if |topic| does not exist.
  const std::string* Get(const std::string& topic) const;

  void Remove(const std::string& topic);

  // Does not take ownership of |listener|. |listener| must remain valid until
  // RemoveSubscription() is called. The returned SubscriptionId can be passed
  // to RemoveSubscription().
  SubscriptionId AddSubscription(ContextQueryPtr query,
                                 ContextListener* listener);
  void RemoveSubscription(SubscriptionId id);

 private:
  void SetInternal(const std::string& topic, const std::string* json_value);
  ContextUpdatePtr BuildContextUpdate(const ContextQueryPtr& query);

  // Keyed by context topic.
  std::unordered_map<std::string, std::string> values_;

  struct Subscription {
    ContextQueryPtr query;
    ContextListener* listener;  // Not owned.
  };
  // We use a std::list<> here instead of a std::vector<> since we capture
  // iterators to identify a subscription for clients, and we want them to
  // remain valid regardless of operations on the container.
  std::list<Subscription> subscriptions_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ContextRepository);
};

}  // namespace maxwell
