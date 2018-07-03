// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CONTEXT_ENGINE_DEBUG_H_
#define PERIDOT_BIN_CONTEXT_ENGINE_DEBUG_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/interface_ptr_set.h>

#include "peridot/bin/context_engine/index.h"
#include "peridot/lib/util/idle_waiter.h"

namespace modular {

class ContextRepository;

class ContextDebugImpl : public fuchsia::modular::ContextDebug {
  using Id = ContextIndex::Id;

 public:
  ContextDebugImpl(const ContextRepository* repository);
  ~ContextDebugImpl();

  fxl::WeakPtr<ContextDebugImpl> GetWeakPtr();

  void OnValueChanged(const std::set<Id>& parent_ids, const Id& id,
                      const fuchsia::modular::ContextValue& value);
  void OnValueRemoved(const Id& id);

  void OnSubscriptionAdded(
      const Id& id, const fuchsia::modular::ContextQuery& query,
      const fuchsia::modular::SubscriptionDebugInfo& debug_info);
  void OnSubscriptionRemoved(const Id& id);

  util::IdleWaiter* GetIdleWaiter();

 private:
  // |fuchsia::modular::ContextDebug|
  void Watch(fidl::InterfaceHandle<fuchsia::modular::ContextDebugListener>
                 listener) override;
  // |fuchsia::modular::ContextDebug|
  void WaitUntilIdle(WaitUntilIdleCallback callback) override;

  void DispatchOneValue(fuchsia::modular::ContextDebugValue value);
  void DispatchValues(
      fidl::VectorPtr<fuchsia::modular::ContextDebugValue> values);
  void DispatchOneSubscription(
      fuchsia::modular::ContextDebugSubscription value);
  void DispatchSubscriptions(
      fidl::VectorPtr<fuchsia::modular::ContextDebugSubscription> values);

  // Used in order to get a complete state snapshot when Watch() is called.
  const ContextRepository* const repository_;
  fidl::InterfacePtrSet<fuchsia::modular::ContextDebugListener> listeners_;

  util::IdleWaiter idle_waiter_;

  fxl::WeakPtrFactory<ContextDebugImpl> weak_ptr_factory_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_CONTEXT_ENGINE_DEBUG_H_
