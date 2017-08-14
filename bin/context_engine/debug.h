// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "apps/maxwell/services/context/debug.fidl.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"

namespace maxwell {

class ContextDebugImpl : public ContextDebug {
  using Subscriptions = std::multimap<ComponentScopePtr,
                                      ContextQueryForTopicsPtr,
                                      bool (*)(const ComponentScopePtr&,
                                               const ComponentScopePtr&)>;

 public:
  using SubscriptionId = Subscriptions::iterator;

  ContextDebugImpl();

  SubscriptionId OnAddSubscription(const ComponentScope& subscriber,
                                   const ContextQueryForTopics& query);

  void OnRemoveSubscription(SubscriptionId subscription);

 private:
  // |ContextDebug|
  void WatchSubscribers(
      fidl::InterfaceHandle<SubscriberListener> listener) override;

  void DispatchAll();
  void Dispatch(SubscriberListener* listener);

  Subscriptions subscriptions_;
  fidl::InterfacePtrSet<SubscriberListener> listeners_;
};

}  // namespace maxwell
