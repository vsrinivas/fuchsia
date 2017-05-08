// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>

#include "apps/maxwell/services/context/context_provider.fidl.h"
#include "apps/maxwell/services/user/scope.fidl.h"
#include "apps/maxwell/src/context_engine/context_repository.h"
#include "apps/maxwell/src/context_engine/debug.h"

namespace maxwell {

class ContextProviderImpl : public ContextProvider {
 public:
  ContextProviderImpl(ComponentScopePtr scope,
                      ContextRepository* repository,
                      ContextDebugImpl* debug);
  ~ContextProviderImpl() override;

 private:
  struct Subscription {
    ContextListenerPtr listener;
    ContextRepository::SubscriptionId repo_subscription_id;
    ContextDebugImpl::SubscriptionId debug_subscription_id;
  };

  // |ContextProvider|
  void Subscribe(ContextQueryPtr query,
                 fidl::InterfaceHandle<ContextListener> listener) override;

  const ComponentScopePtr scope_;
  ContextRepository* repository_;
  ContextDebugImpl* debug_;

  // We use a std::list<> here instead of a std::vector<> since we capture
  // iterators in |listeners_| for removing elements in our connection
  // error handler.
  std::list<Subscription> listeners_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ContextProviderImpl);
};

}  // namespace maxwell
