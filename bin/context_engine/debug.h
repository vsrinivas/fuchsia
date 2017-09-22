// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "lib/context/fidl/debug.fidl.h"
#include "peridot/bin/context_engine/index.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"

namespace maxwell {

class ContextRepository;

class ContextDebugImpl : public ContextDebug {
  using Id = ContextIndex::Id;
 public:
  ContextDebugImpl(const ContextRepository* repository);
  ~ContextDebugImpl();

  void OnValueChanged(const std::set<Id>& parent_ids,
                      const Id& id,
                      const ContextValuePtr& value);
  void OnValueRemoved(const Id& id);

  void OnSubscriptionAdded(const Id& id,
                           const ContextQueryPtr& query,
                           const SubscriptionDebugInfoPtr& debug_info);
  void OnSubscriptionRemoved(const Id& id);

 private:
  // |ContextDebug|
  void Watch(fidl::InterfaceHandle<ContextDebugListener> listener) override;

  void DispatchOneValue(ContextDebugValuePtr value);
  void DispatchValues(fidl::Array<ContextDebugValuePtr> values);
  void DispatchOneSubscription(ContextDebugSubscriptionPtr value);
  void DispatchSubscriptions(fidl::Array<ContextDebugSubscriptionPtr> values);

  // Used in order to get a complete state snapshot when Watch() is called.
  const ContextRepository* const repository_;
  fidl::InterfacePtrSet<ContextDebugListener> listeners_;
};

}  // namespace maxwell
