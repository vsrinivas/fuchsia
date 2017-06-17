// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <map>

#include "apps/maxwell/services/context/context_provider.fidl.h"

namespace maxwell {

class ContextCoprocessor;  // See below for definition.

// Tracks current values of context topics as well as subscriptions to those
// topics. Is responsible for notifying any subscribed clients whenever a topic
// changes value.
//
// Additionally supports coprocessors (extensions to change how new topic
// values are processed), and provides convenience methods for retrieving
// context values across various scopes.
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
  // RemoveSubscription() is called with the returned SubscriptionId.
  SubscriptionId AddSubscription(ContextQueryPtr query,
                                 ContextListener* listener);
  void RemoveSubscription(SubscriptionId id);

  // Add a ContextCoprocessor. Coprocessors are executed in order as part of
  // SetInternal().  See documentation for ContextCoprocessor below.
  //
  // Takes ownership of |coprocessor|.
  void AddCoprocessor(ContextCoprocessor* coprocessor);

  // Stores into |output| a list of all values for a given topic across all
  // Modules within the scope of a specific Story.
  void GetAllValuesInStoryScope(const std::string& story_id,
                                const std::string& topic,
                                std::vector<std::string>* output) const;

  // Stores into |output| all context topics with the given string prefix.
  void GetAllTopicsWithPrefix(const std::string& prefix,
                              std::vector<std::string>* output) const;

 private:
  void SetInternal(const std::string& topic, const std::string* json_value);
  ContextUpdatePtr BuildContextUpdate(const ContextQueryPtr& query);

  // Keyed by context topic.
  std::map<std::string, std::string> values_;

  struct Subscription {
    ContextQueryPtr query;
    ContextListener* listener;  // Not owned.
  };
  // We use a std::list<> here instead of a std::vector<> since we capture
  // iterators to identify a subscription for clients, and we want them to
  // remain valid regardless of operations on the container.
  std::list<Subscription> subscriptions_;

  std::vector<std::unique_ptr<ContextCoprocessor>> coprocessors_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ContextRepository);
};

// Executed as part of ContextRepository::SetInternal(). Every
// ContextCoprocessor that was added to a ContextRepository (through
// AddCoprocessor()) is executed in order when SetInternal() is called. The
// Coprocessor recieves the current context state, as well as a list of topics
// that have been updated thus far, and can instruct the ContextRepository to
// create additional updates.
class ContextCoprocessor {
 public:
  virtual ~ContextCoprocessor();

  // Recieves a |repository| which represents current state, as well as a list
  // of |topics_updated| so far. The Coprocessor can optionally populate |out|
  // with additional topics and values to update.
  virtual void ProcessTopicUpdate(const ContextRepository* repository,
                                  const std::set<std::string>& topics_updated,
                                  std::map<std::string, std::string>* out) = 0;
};

}  // namespace maxwell
