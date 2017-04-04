// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>
#include <unordered_map>

#include "apps/maxwell/services/context/context_subscriber.fidl.h"
#include "apps/maxwell/src/bound_set.h"

namespace maxwell {

// Tracks current values of context topics as well as subscriptions to those
// topics. Is responsible for notifying any subscribed clients whenever a topic
// changes value.
class Repo {
 public:
  Repo() {}

  void Set(const std::string& topic, const std::string& json_value);
  void Remove(const std::string& topic);

  void AddSubscription(const std::string& topic,
                       ContextSubscriberLinkPtr subscriber);

 private:
  void SetInternal(const std::string& topic, const std::string* json_value);

  // Keyed by context topic.
  std::unordered_map<std::string, std::string> values_;
  std::unordered_map<std::string, BoundPtrSet<ContextSubscriberLink>>
      subscriptions_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Repo);
};

}  // namespace maxwell
