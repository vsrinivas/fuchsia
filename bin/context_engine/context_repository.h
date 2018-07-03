// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_REPOSITORY_H_
#define PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_REPOSITORY_H_

#include <map>
#include <set>
#include <string>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/context_engine/index.h"

namespace modular {

class ContextDebug;
class ContextDebugImpl;

// This class represents a "multiparent hierarchy", which is another way
// of saying a directed graph that cannot have any cycles.
// TODO(thatguy): Actually enforce the no cycles constraint :).
class ContextGraph {
 public:
  using Id = std::string;

  ContextGraph();
  virtual ~ContextGraph();

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

// Stores a graph of fuchsia::modular::ContextValue structs (values). Supports
// fetching lists of values based on a) the value's type and b)
// fuchsia::modular::ContextMetadata fields.
//
// The graph structure is used to represent value namespaces or scope, although
// the exact meaning or what concepts are represented is up to the client. When
// a value is queried against and returned, its metadata is "flattened" with
// its ancestors' metadata. For example, if an ENTITY value is a child of a
// MODULE value, the ENTITY value will inherit the MODULE value's metadata (ie,
// the |mod| field of the fuchsia::modular::ContextMetadata struct).
class ContextRepository {
  struct ValueInternal;
  struct Subscription;
  struct InProgressUpdate;

 public:
  using Id = ContextIndex::Id;
  using IdAndVersionSet = std::set<std::pair<Id, uint32_t>>;

  ContextRepository();
  ~ContextRepository();

  bool Contains(const Id& id) const;
  Id Add(fuchsia::modular::ContextValue value);
  Id Add(const Id& parent_id, fuchsia::modular::ContextValue value);
  void Update(const Id& id, fuchsia::modular::ContextValue value);
  void Remove(const Id& id);

  // Returns a copy of the fuchsia::modular::ContextValue for |id|. Returns a
  // null |fuchsia::modular::ContextValuePtr| if |id| is not valid.
  fuchsia::modular::ContextValuePtr Get(const Id& id) const;

  // Returns a copy of the fuchsia::modular::ContextValue for |id|, with
  // metadata merged from ancestors. Returns a null
  // |fuchsia::modular::ContextValuePtr| if |id| is not valid.
  fuchsia::modular::ContextValuePtr GetMerged(const Id& id) const;

  std::set<Id> Select(const fuchsia::modular::ContextSelector& selector);

  // Returns the current requested values for the given query as a context
  // update.
  fuchsia::modular::ContextUpdate Query(
      const fuchsia::modular::ContextQuery& query);

  // Does not take ownership of |listener|. |listener| must remain valid until
  // RemoveSubscription() is called with the returned Id.
  Id AddSubscription(fuchsia::modular::ContextQuery query,
                     fuchsia::modular::ContextListener* listener,
                     fuchsia::modular::SubscriptionDebugInfo debug_info);
  void RemoveSubscription(Id id);

  // Like AddSubscription above, but takes ownership of the FIDL service proxy
  // object, |listener|. The subscription is automatically removed when
  // |listener| experiences a connection error.
  void AddSubscription(fuchsia::modular::ContextQuery query,
                       fuchsia::modular::ContextListenerPtr listener,
                       fuchsia::modular::SubscriptionDebugInfo debug_info);

  ContextDebugImpl* debug();
  void AddDebugBinding(
      fidl::InterfaceRequest<fuchsia::modular::ContextDebug> request);

 private:
  Id AddInternal(const Id& parent_id, fuchsia::modular::ContextValue value);
  void RecomputeMergedMetadata(ValueInternal* value);
  void ReindexAndNotify(InProgressUpdate update);
  void QueryAndMaybeNotify(Subscription* subscription, bool force);
  std::pair<fuchsia::modular::ContextUpdate, IdAndVersionSet> QueryInternal(
      const fuchsia::modular::ContextQuery& query);

  // Keyed by internal id.
  std::map<Id, ValueInternal> values_;
  ContextGraph graph_;

  // A map of Id (int) to Subscription.
  std::map<Id, Subscription> subscriptions_;

  ContextIndex index_;

  friend class ContextDebugImpl;
  std::unique_ptr<ContextDebugImpl> debug_;
  fidl::BindingSet<fuchsia::modular::ContextDebug> debug_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextRepository);
};

struct ContextRepository::ValueInternal {
  // The contents of |value.meta| merged with metadata from all
  // of this value's ancestors.
  Id id;
  fuchsia::modular::ContextMetadata merged_metadata;
  fuchsia::modular::ContextValue value;
  uint32_t version;  // Incremented on change.
};

struct ContextRepository::Subscription {
  fuchsia::modular::ContextQuery query;
  fuchsia::modular::ContextListener*
      listener;  // Optionally owned by |listener_storage|.
  fuchsia::modular::ContextListenerPtr listener_storage;
  fuchsia::modular::SubscriptionDebugInfo debug_info;
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

}  // namespace modular

#endif  // PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_REPOSITORY_H_
