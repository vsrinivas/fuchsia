// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CONTEXT_ENGINE_DEBUG_H_
#define PERIDOT_BIN_CONTEXT_ENGINE_DEBUG_H_

#include <map>

#include "lib/context/fidl/debug.fidl.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/context_engine/index.h"

namespace maxwell {

class ContextRepository;

class ContextDebugImpl : public ContextDebug {
  using Id = ContextIndex::Id;

 public:
  class Activity : public fxl::RefCountedThreadSafe<Activity> {
   public:
    Activity(fxl::WeakPtr<ContextDebugImpl> debug);
    ~Activity();

   private:
    fxl::WeakPtr<ContextDebugImpl> debug_;
  };

  using ActivityToken = fxl::RefPtr<Activity>;

  // Must be constructed on a message loop thread.
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

  // Registers an ongoing activity which prevents context engine from being
  // considered idle. When the weak pointer is released, the activity is
  // considered complete.
  //
  // |ActivityToken| should typically be captured in any lambda triggered while
  // handling a call. It is not necessary to register an activity that completes
  // synchronously.
  //
  // This method must be invoked on the creation thread.
  ActivityToken RegisterOngoingActivity();

  // Idle checks must be performed outside of the main message loop, so
  // |PostIdleCheck| escapes the main message loop with a quit task. When this
  // happens, context engine main should call |FinishIdleCheck| and then resume
  // the main message loop if it returns |true|.
  //
  // TODO(rosswang): Remove this requirement if |RunUntilIdle| is ever supported
  //  from within an outer message loop.
  bool FinishIdleCheck();

 private:
  // |ContextDebug|
  void Watch(f1dl::InterfaceHandle<ContextDebugListener> listener) override;
  // |ContextDebug|
  void WaitUntilIdle(const WaitUntilIdleCallback& callback) override;

  void DispatchOneValue(ContextDebugValuePtr value);
  void DispatchValues(f1dl::VectorPtr<ContextDebugValuePtr> values);
  void DispatchOneSubscription(ContextDebugSubscriptionPtr value);
  void DispatchSubscriptions(
      f1dl::VectorPtr<ContextDebugSubscriptionPtr> values);

  // See |FinishIdleCheck| comment.
  void PostIdleCheck();

  // Used in order to get a complete state snapshot when Watch() is called.
  const ContextRepository* const repository_;
  f1dl::InterfacePtrSet<ContextDebugListener> listeners_;

  fsl::MessageLoop* const message_loop_;
  std::vector<WaitUntilIdleCallback> idle_waiters_;
  Activity* activity_ = nullptr;
  bool idle_check_pending_ = false;

  fxl::WeakPtrFactory<ContextDebugImpl> weak_ptr_factory_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_CONTEXT_ENGINE_DEBUG_H_
