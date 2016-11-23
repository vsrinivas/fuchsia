// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/dart/sdk_ext/src/handle_waiter.h"

#include "lib/fidl/cpp/waiter/default.h"
#include "lib/tonic/converter/dart_converter.h"
#include "lib/tonic/dart_args.h"
#include "lib/tonic/dart_binding_macros.h"
#include "lib/tonic/dart_library_natives.h"
#include "lib/tonic/logging/dart_invoke.h"

using tonic::DartInvokeField;
using tonic::DartState;
using tonic::ToDart;

namespace fidl {
namespace dart {

static void HandleWaiter_constructor(Dart_NativeArguments args) {
  DartCallConstructor(&HandleWaiter::Create, args);
}

IMPLEMENT_WRAPPERTYPEINFO(fidl, HandleWaiter);

#define FOR_EACH_BINDING(V)  \
  V(HandleWaiter, asyncWait) \
  V(HandleWaiter, cancelWait)

FOR_EACH_BINDING(DART_NATIVE_CALLBACK)

void HandleWaiter::RegisterNatives(tonic::DartLibraryNatives* natives) {
  natives->Register(
      {{"HandleWaiter_constructor", HandleWaiter_constructor, 2, true},
       FOR_EACH_BINDING(DART_REGISTER_NATIVE)});
}

ftl::RefPtr<HandleWaiter> HandleWaiter::Create(std::string stack) {
  return ftl::MakeRefCounted<HandleWaiter>(std::move(stack));
}

HandleWaiter::HandleWaiter(std::string stack)
    : waiter_(GetDefaultAsyncWaiter()),
      dart_state_(DartState::Current()->GetWeakPtr()),
      creation_stack_(std::move(stack)) {}

HandleWaiter::~HandleWaiter() {
  if (wait_id_) {
    if (Dart_CurrentIsolate() && !Dart_GetMessageNotifyCallback()) {
      // The current isolate is shutting down and it is safe to cancel the wait.
      cancelWait();
    } else {
      if (creation_stack_.empty()) {
        FTL_LOG(FATAL) << "Failed to cancel wait " << wait_id_ << ".";
      } else {
        FTL_LOG(FATAL) << "Failed to cancel wait for waiter created at:\n"
                       << creation_stack_;
      }
    }
  }
}

void HandleWaiter::asyncWait(mx_handle_t handle,
                             mx_signals_t signals,
                             mx_time_t timeout) {
  cancelWait();
  wait_id_ = waiter_->AsyncWait(handle, signals, timeout,
                                HandleWaiter::CallOnWaitComplete, this);
}

void HandleWaiter::cancelWait() {
  if (wait_id_) {
    waiter_->CancelWait(wait_id_);
    wait_id_ = 0;
  }
}

void HandleWaiter::OnWaitComplete(mx_status_t status, mx_signals_t pending) {
  FTL_DCHECK(wait_id_);
  wait_id_ = 0;

  if (!dart_state_)
    return;
  DartState::Scope scope(dart_state_.get());
  Dart_Handle wrapper = Dart_HandleFromWeakPersistent(dart_wrapper());
  // The wrapper might be null if we've leaked the waiter and its finalizer
  // hasn't run yet. When the finalizer runs, we'll terminate the process.
  if (Dart_IsNull(wrapper))
    return;
  DartInvokeField(wrapper, "onWaitComplete", {ToDart(status), ToDart(pending)});
}

void HandleWaiter::CallOnWaitComplete(mx_status_t status,
                                      mx_signals_t pending,
                                      void* closure) {
  HandleWaiter* handle_waiter = static_cast<HandleWaiter*>(closure);
  handle_waiter->OnWaitComplete(status, pending);
}

}  // namespace dart
}  // namespace fidl
