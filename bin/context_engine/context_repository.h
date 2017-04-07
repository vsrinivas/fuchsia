// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <unordered_map>

#include "apps/maxwell/services/context/context_subscriber.fidl.h"

namespace maxwell {

// Tracks current values of context topics as well as subscriptions to those
// topics. Is responsible for notifying any subscribed clients whenever a topic
// changes value.
class ContextRepository {
 public:
  ContextRepository();
  ~ContextRepository();

  void Set(const std::string& topic, const std::string& json_value);
  void Remove(const std::string& topic);

  void AddSubscription(ContextQueryPtr query, ContextListenerPtr listener);

 private:
  void SetInternal(const std::string& topic, const std::string* json_value);
  ContextUpdatePtr BuildContextUpdate(const ContextQueryPtr& query);

  // Keyed by context topic.
  std::unordered_map<std::string, std::string> values_;

  struct Subscription {
    ContextQueryPtr query;
    ContextListenerPtr listener;
  };
  // We use a std::list<> here instead of a std::vector<> since we capture
  // iterators in |subscriptions_| for removing elements in our connection
  // error handler.
  std::list<Subscription> subscriptions_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ContextRepository);
};

}  // namespace maxwell
