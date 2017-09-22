// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <set>
#include <string>

#include "apps/maxwell/services/context/context_reader.fidl.h"
#include "apps/maxwell/services/context/debug.fidl.h"
#include "apps/maxwell/services/context/metadata.fidl.h"
#include "apps/maxwell/services/context/value.fidl.h"
#include "apps/maxwell/src/context_engine/index.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

namespace maxwell {

class ContextDebug;
class ContextDebugImpl;

// This class represents a "multiparent hierarchy", which is another way
// of saying a directed graph that cannot have any cycles.
// TODO(thatguy): Actually enforce the no cycles constraint :).
class ContextGraph {
 public:
  using Id = std::string;
  // Adds a graph edge from |from| to |to|.
  void AddEdge(const Id& from, const Id& to);

  // Removes the node |id| and removes any incoming or outgoing edges.
  // TODO(thatguy): Decide what to do about orphaned children.
  void Remove(const Id& id);

  std::set<Id> GetParents(const Id& id) const;

  // Returns all |id|'s children and their children, recursively.
  std::set<Id> GetChildrenRecursive(const Id& id) const;

  // Returns all ancestors for |id|, guaranteeing that all node ids appear in
  // the return value before their children (ie, in order of seniority).
  std::vector<Id> GetAncestors(const Id& id) const;

 private:
  // From node to its parents.
  std::map<Id, std::set<Id>> parents_;
  // From parent to its immediate children.
  std::map<Id, std::set<Id>> children_;
};

// Stores a graph of ContextValue structs (values). Supports fetching lists of
// values based on a) the value's type and b) ContextMetadata fields.
//
// The graph structure is used to represent value namespaces or scope, although
// the exact meaning or what concepts are represented is up to the client. When
// a value is queried against and returned, its metadata is "flattened" with
// its ancestors' metadata. For example, if an ENTITY value is a child of a
// MODULE value, the ENTITY value will inherit the MODULE value's metadata (ie,
// the |mod| field of the ContextMetadata struct).
class ContextRepository {
  struct ValueInternal;
  struct Subscription;
  struct InProgressUpdate;

 public:
  using Id = ContextIndex::Id;

  ContextRepository();
  ~ContextRepository();

  bool Contains(const Id& id) const;
  Id Add(ContextValuePtr value);
  Id Add(const Id& parent_id, ContextValuePtr value);
  void Update(const Id& id, ContextValuePtr value);
  void Remove(const Id& id);

  // Returns a copy of the ContextValue for |id|. Returns a null
  // |ContextValuePtr| if |id| is not valid.
  ContextValuePtr Get(const Id& id) const;

  // Returns a copy of the ContextValue for |id|, with metadata merged
  // from ancestors. Returns a null |ContextValuePtr| if |id| is not valid.
  ContextValuePtr GetMerged(const Id& id) const;

  std::set<Id> Select(const ContextSelectorPtr& selector);

  // Does not take ownership of |listener|. |listener| must remain valid until
  // RemoveSubscription() is called with the returned Id.
  Id AddSubscription(ContextQueryPtr query,
                     ContextListener* listener,
                     SubscriptionDebugInfoPtr debug_info);
  void RemoveSubscription(Id id);

  // Like AddSubscription above, but takes ownership of the FIDL service proxy
  // object, |listener|. The subscription is automatically removed when
  // |listener| experiences a connection error.
  void AddSubscription(ContextQueryPtr query,
                       ContextListenerPtr listener,
                       SubscriptionDebugInfoPtr debug_info);

  void AddDebugBinding(fidl::InterfaceRequest<ContextDebug> request);

 private:
  Id AddInternal(const Id& parent_id, ContextValuePtr value);
  void RecomputeMergedMetadata(ValueInternal* value);
  void ReindexAndNotify(InProgressUpdate update);
  void QueryAndMaybeNotify(Subscription* subscription, bool force);

  // Keyed by internal id.
  std::map<Id, ValueInternal> values_;
  ContextGraph graph_;

  // A map of Id (int) to Subscription.
  std::map<Id, Subscription> subscriptions_;

  ContextIndex index_;

  friend class ContextDebugImpl;
  std::unique_ptr<ContextDebugImpl> debug_;
  fidl::BindingSet<ContextDebug> debug_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextRepository);
};

struct ContextRepository::ValueInternal {
  // The contents of |value.meta| merged with metadata from all
  // of this value's ancestors.
  Id id;
  ContextMetadataPtr merged_metadata;
  ContextValuePtr value;
  uint32_t version;  // Incremented on change.
};

struct ContextRepository::Subscription {
  using IdAndVersionSet = std::set<std::pair<Id, uint32_t>>;

  ContextQueryPtr query;
  ContextListener* listener;  // Optionally owned by |listener_storage|.
  ContextListenerPtr listener_storage;
  SubscriptionDebugInfoPtr debug_info;
  // The set of value id and version we sent the last time we notified
  // |listener|. Used to calculate if a new update is different.
  IdAndVersionSet last_update;
};

// Holds interim values necessary for processing an update to at least one
// context value.
struct ContextRepository::InProgressUpdate {
  // These values are having their values added or updated.
  std::vector<ValueInternal*> updated_values;
  // These values are being removed.
  std::vector<ValueInternal> removed_values;
};

}  // namespace maxwell
