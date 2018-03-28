// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CONTEXT_ENGINE_DEBUG_H_
#define PERIDOT_BIN_CONTEXT_ENGINE_DEBUG_H_

#include <fuchsia/cpp/modular.h>
#include "lib/fidl/cpp/interface_ptr_set.h"
#include "peridot/bin/context_engine/index.h"
#include "peridot/lib/util/wait_until_idle.h"

namespace modular {

class ContextRepository;

class ContextDebugImpl : public ContextDebug {
  using Id = ContextIndex::Id;

 public:
  ContextDebugImpl(const ContextRepository* repository);
  ~ContextDebugImpl();

  fxl::WeakPtr<ContextDebugImpl> GetWeakPtr();

  void OnValueChanged(const std::set<Id>& parent_ids,
                      const Id& id,
                      const ContextValuePtr& value);
  void OnValueRemoved(const Id& id);

  void OnSubscriptionAdded(const Id& id,
                           const ContextQueryPtr& query,
                           const SubscriptionDebugInfoPtr& debug_info);
  void OnSubscriptionRemoved(const Id& id);

  // Forwards to |IdleWaiter::RegisterOngoingActivity|
  util::IdleWaiter::ActivityToken RegisterOngoingActivity();
  // Forwards to |IdleWaiter::FinishIdleCheck|
  bool FinishIdleCheck();

 private:
  // |ContextDebug|
  void Watch(fidl::InterfaceHandle<ContextDebugListener> listener) override;
  // |ContextDebug|
  void WaitUntilIdle(WaitUntilIdleCallback callback) override;

  void DispatchOneValue(ContextDebugValuePtr value);
  void DispatchValues(fidl::VectorPtr<ContextDebugValuePtr> values);
  void DispatchOneSubscription(ContextDebugSubscriptionPtr value);
  void DispatchSubscriptions(
      fidl::VectorPtr<ContextDebugSubscriptionPtr> values);

  // Used in order to get a complete state snapshot when Watch() is called.
  const ContextRepository* const repository_;
  fidl::InterfacePtrSet<ContextDebugListener> listeners_;

  util::IdleWaiter wait_until_idle_;

  fxl::WeakPtrFactory<ContextDebugImpl> weak_ptr_factory_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_CONTEXT_ENGINE_DEBUG_H_
