// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>
#include <memory>
#include <set>

#include "apps/maxwell/src/context_engine/context_repository.h"
#include "apps/maxwell/src/context_engine/debug.h"

#include "apps/maxwell/lib/context/formatting.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "rapidjson/document.h"

namespace maxwell {

void ContextGraph::AddEdge(const Id& from, const Id& to) {
  parents_[to].insert(from);
  children_[from].insert(to);
}
void ContextGraph::Remove(const Id& id) {
  auto parents = GetParents(id);
  for (const auto& parent : parents) {
    children_[parent].erase(id);
  }
  for (const auto& child : children_[id]) {
    parents_[child].erase(id);
  }
  // TODO(thatguy): Decide what to do about orphaned children.
  children_.erase(id);
  parents_.erase(id);
}

std::set<ContextGraph::Id> ContextGraph::GetParents(const Id& id) const {
  auto it = parents_.find(id);
  if (it == parents_.end())
    return {};
  return it->second;
}

std::set<ContextGraph::Id> ContextGraph::GetChildrenRecursive(
    const Id& id) const {
  std::set<Id> children;
  std::list<Id> to_visit{id};

  while (!to_visit.empty()) {
    auto cur = to_visit.front();
    to_visit.pop_front();

    auto it = children_.find(cur);
    if (it != children_.end()) {
      children.insert(it->second.begin(), it->second.end());
      to_visit.insert(to_visit.begin(), it->second.begin(), it->second.end());
    }
  }

  return children;
}

std::vector<ContextGraph::Id> ContextGraph::GetAncestors(const Id& id) const {
  std::vector<Id> ancestors;
  std::set<Id> visited;
  std::list<Id> to_visit;

  auto it = parents_.find(id);
  if (it == parents_.end())
    return ancestors;
  to_visit.insert(to_visit.begin(), it->second.begin(), it->second.end());
  while (!to_visit.empty()) {
    auto cur = to_visit.front();
    to_visit.pop_front();
    if (!visited.insert(cur).second)
      continue;
    ancestors.insert(ancestors.begin(), cur);
    it = parents_.find(cur);
    if (it != parents_.end()) {
      to_visit.insert(to_visit.end(), it->second.begin(), it->second.end());
    }
  }
  return ancestors;
}

namespace {

ContextRepository::Id CreateValueId() {
  // TODO(thatguy): Prefer to use a string UUID.
  static uint32_t next_id = 0;
  return std::to_string(next_id++);
}

ContextRepository::Id CreateSubscriptionId() {
  static uint32_t next_id = 0;
  return std::to_string(next_id++);
}

void LogInvalidAncestorMetadata(const ContextMetadataPtr& from,
                                ContextMetadataPtr* to,
                                const char* type) {
  FXL_LOG(WARNING) << "Context value and ancestor both have metadata type ("
                   << type
                   << "), which is not allowed. Ignoring value metadata.";
  FXL_LOG(WARNING) << "Value metadata: " << *to;
  FXL_LOG(WARNING) << "Ancestor metadata: " << from;
}

void MergeMetadata(const ContextMetadataPtr& from, ContextMetadataPtr* to) {
#define MERGE(type)                                \
  if (from && from->type) {                        \
    if (!*to) {                                    \
      *to = ContextMetadata::New();                \
    }                                              \
    if ((*to)->type) {                             \
      LogInvalidAncestorMetadata(from, to, #type); \
    } else {                                       \
      (*to)->type = from->type.Clone();            \
    }                                              \
  }
  // Go through each type of metadata on |from|, and copy it to
  // |*to|. However, if anything in |from| already exists in |*to|,
  // that's an error, since a value's ancestors can only have one
  // node of each type (story, module, entity, etc).
  MERGE(story);
  MERGE(mod);
  MERGE(entity);
#undef MERGE
}

}  // namespace

ContextRepository::ContextRepository() : debug_(new ContextDebugImpl(this)) {}
ContextRepository::~ContextRepository() = default;

bool ContextRepository::Contains(const Id& id) const {
  return values_.find(id) != values_.end();
}

ContextRepository::Id ContextRepository::Add(const Id& parent_id,
                                             ContextValuePtr value) {
  FXL_DCHECK(values_.find(parent_id) != values_.end()) << parent_id;
  return AddInternal(parent_id, std::move(value));
}

ContextRepository::Id ContextRepository::Add(ContextValuePtr value) {
  return AddInternal("", std::move(value));
}

void ContextRepository::Update(const Id& id, ContextValuePtr value) {
  // TODO(thatguy): Short-circuit if |value| isn't changing anything to avoid
  // spurious update computation.

  auto it = values_.find(id);
  FXL_DCHECK(it != values_.end()) << id;
  it->second.value = std::move(value);
  it->second.version++;

  InProgressUpdate update;
  update.updated_values.push_back(&it->second);
  // Updating a value can affect all of its children, so we need to re-process
  // and reindex them.
  auto children = graph_.GetChildrenRecursive(id);
  for (const auto& child : children) {
    auto child_it = values_.find(child);
    FXL_DCHECK(child_it != values_.end()) << child;
    update.updated_values.push_back(&child_it->second);
  }
  ReindexAndNotify(std::move(update));
  debug_->OnValueChanged(graph_.GetParents(id), id, it->second.value);
}

ContextRepository::Id ContextRepository::AddInternal(const Id& parent_id,
                                                     ContextValuePtr value) {
  FXL_DCHECK(value);
  const auto new_id = CreateValueId();

  // Add the new value to |values_|.
  ValueInternal value_internal;
  value_internal.id = new_id;
  value_internal.version = 0;

  value_internal.value = std::move(value);
  auto it = values_.emplace(new_id, std::move(value_internal));

  if (!parent_id.empty()) {
    // Update the graph.
    graph_.AddEdge(parent_id, new_id);
  }

  InProgressUpdate update;
  update.updated_values.push_back(&it.first->second);
  ReindexAndNotify(std::move(update));

  if (parent_id.empty()) {
    debug_->OnValueChanged({}, new_id, it.first->second.value);
  } else {
    debug_->OnValueChanged({parent_id}, new_id, it.first->second.value);
  }
  return new_id;
}

void ContextRepository::Remove(const Id& id) {
  auto it = values_.find(id);
  if (it == values_.end()) {
    FXL_LOG(WARNING) << "Attempting to remove non-existent value: " << id;
    return;
  }
  InProgressUpdate update;
  update.removed_values.push_back(std::move(it->second));
  // Removing a value can affect all of its children, so we need to re-process
  // and reindex them.
  auto children = graph_.GetChildrenRecursive(id);
  for (const auto& child : children) {
    auto child_it = values_.find(child);
    FXL_DCHECK(child_it != values_.end()) << child;
    update.updated_values.push_back(&child_it->second);
  }
  values_.erase(it);
  graph_.Remove(id);

  debug_->OnValueRemoved(id);
  ReindexAndNotify(std::move(update));
}

ContextValuePtr ContextRepository::Get(const Id& id) const {
  auto it = values_.find(id);
  if (it == values_.end())
    return ContextValuePtr();

  return it->second.value->Clone();
}

ContextValuePtr ContextRepository::GetMerged(const Id& id) const {
  auto it = values_.find(id);
  if (it == values_.end())
    return ContextValuePtr();

  ContextValuePtr merged_value = it->second.value->Clone();
  // Copy the merged metadata (includes ancestor metadata).
  merged_value->meta = it->second.merged_metadata.Clone();
  return merged_value;
}

ContextRepository::Id ContextRepository::AddSubscription(
    ContextQueryPtr query,
    ContextListener* const listener,
    SubscriptionDebugInfoPtr debug_info) {
  // Add the subscription to our list.
  Subscription subscription;
  subscription.query = std::move(query);
  subscription.listener = listener;
  subscription.debug_info = std::move(debug_info);
  const auto id = CreateSubscriptionId();
  auto it = subscriptions_.emplace(id, std::move(subscription));
  FXL_DCHECK(it.second);

  // Notify the listener immediately of our current state.
  QueryAndMaybeNotify(&it.first->second, true /* force */);

  // TODO(thatguy): Add a client identifier parameter to AddSubscription().
  debug_->OnSubscriptionAdded(id, it.first->second.query,
                              it.first->second.debug_info);
  return id;
}

void ContextRepository::AddSubscription(ContextQueryPtr query,
                                        ContextListenerPtr listener,
                                        SubscriptionDebugInfoPtr debug_info) {
  auto id =
      AddSubscription(std::move(query), listener.get(), std::move(debug_info));
  listener.set_connection_error_handler([this, id] { RemoveSubscription(id); });
  // RemoveSubscription() above is responsible for freeing this memory.
  subscriptions_[id].listener_storage = std::move(listener);
}

void ContextRepository::AddDebugBinding(
    fidl::InterfaceRequest<ContextDebug> request) {
  debug_bindings_.AddBinding(debug_.get(), std::move(request));
}

void ContextRepository::RemoveSubscription(Id id) {
  auto it = subscriptions_.find(id);
  FXL_DCHECK(it != subscriptions_.end());
  subscriptions_.erase(it);

  debug_->OnSubscriptionRemoved(id);
}

void ContextRepository::QueryAndMaybeNotify(Subscription* const subscription,
                                            bool force) {
  // For each entry in |query->selector|, query the index for matching values.
  Subscription::IdAndVersionSet matching_id_version;
  ContextUpdatePtr update = ContextUpdate::New();
  for (auto entry : subscription->query->selector) {
    const auto& key = entry.GetKey();
    const auto& selector = entry.GetValue();

    std::set<ContextIndex::Id> values;
    index_.Query(selector->type, selector->meta, &values);

    update->values[key] = fidl::Array<ContextValuePtr>::New(0);
    for (const auto& id : values) {
      auto it = values_.find(id);
      FXL_DCHECK(it != values_.end()) << id;
      matching_id_version.insert(std::make_pair(id, it->second.version));
      update->values[key].push_back(GetMerged(id));
    }
  }

  if (!force) {
    // Check if this update contains any new values.
    Subscription::IdAndVersionSet diff;
    std::set_symmetric_difference(
        matching_id_version.begin(), matching_id_version.end(),
        subscription->last_update.begin(), subscription->last_update.end(),
        std::inserter(diff, diff.begin()));
    if (diff.empty()) {
      return;
    }
  }
  subscription->last_update = matching_id_version;

  subscription->listener->OnContextUpdate(std::move(update));
}

void ContextRepository::ReindexAndNotify(
    ContextRepository::InProgressUpdate update) {
  for (auto* value : update.updated_values) {
    // Step 1: reindex the value.

    // Before we add the newest metadata values to the index, we need to remove
    // the old values from the index. |value.merged_metadata| contains whatever
    // we added to the index last time.
    index_.Remove(value->id, value->value->type, value->merged_metadata);
    RecomputeMergedMetadata(value);
    index_.Add(value->id, value->value->type, value->merged_metadata);
  }

  // Step 3: recompute the output for each subscription and notify its
  // listeners.
  for (auto& it : subscriptions_) {
    QueryAndMaybeNotify(&it.second, false /* force */);
  }
}

void ContextRepository::RecomputeMergedMetadata(ValueInternal* const value) {
  value->merged_metadata = ContextMetadataPtr();
  // It doesn't matter what order we merge the ancestor values, because there
  // should always only be one of each type, and thus no collisions.
  std::vector<Id> ancestors = graph_.GetAncestors(value->id);
  for (const auto& ancestor_id : ancestors) {
    const auto& it = values_.find(ancestor_id);
    FXL_DCHECK(it != values_.end());
    const auto& ancestor_value = it->second;
    MergeMetadata(ancestor_value.value->meta, &value->merged_metadata);
  }
  MergeMetadata(value->value->meta, &value->merged_metadata);
}

std::set<ContextRepository::Id> ContextRepository::Select(
    const ContextSelectorPtr& selector) {
  std::set<ContextIndex::Id> values;
  index_.Query(selector->type, selector->meta, &values);
  return values;
}

}  // namespace maxwell
