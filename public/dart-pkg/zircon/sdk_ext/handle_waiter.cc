// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dart-pkg/zircon/sdk_ext/handle_waiter.h"

#include "lib/fidl/cpp/waiter/default.h"
#include "dart-pkg/zircon/sdk_ext/handle.h"
#include "lib/tonic/converter/dart_converter.h"
#include "lib/tonic/dart_args.h"
#include "lib/tonic/dart_binding_macros.h"
#include "lib/tonic/dart_library_natives.h"
#include "lib/tonic/logging/dart_invoke.h"

using tonic::DartInvokeField;
using tonic::DartState;
using tonic::ToDart;

namespace zircon {
namespace dart {

IMPLEMENT_WRAPPERTYPEINFO(zircon, HandleWaiter);

#define FOR_EACH_BINDING(V) \
  V(HandleWaiter, Cancel)

FOR_EACH_BINDING(DART_NATIVE_CALLBACK)

void HandleWaiter::RegisterNatives(tonic::DartLibraryNatives* natives) {
  natives->Register({FOR_EACH_BINDING(DART_REGISTER_NATIVE)});
}

fxl::RefPtr<HandleWaiter> HandleWaiter::Create(Handle* handle,
                                               mx_signals_t signals,
                                               Dart_Handle callback) {
  return fxl::MakeRefCounted<HandleWaiter>(handle, signals, callback);
}

HandleWaiter::HandleWaiter(Handle* handle,
                           mx_signals_t signals,
                           Dart_Handle callback)
    : waiter_(fidl::GetDefaultAsyncWaiter()),
      handle_(handle),
      callback_(DartState::Current(), callback) {
  FXL_CHECK(handle_ != nullptr);
  FXL_CHECK(handle_->is_valid());

  wait_id_ = waiter_->AsyncWait(handle_->handle(), signals, MX_TIME_INFINITE,
                                HandleWaiter::CallOnWaitComplete, this);
  FXL_DCHECK(wait_id_ != 0);
}

HandleWaiter::~HandleWaiter() {
  // Destructor shouldn't be called until it has been released from its
  // Handle.
  FXL_DCHECK(!handle_);
  // Destructor shouldn't be called until the wait has completed or been
  // cancelled.
  FXL_DCHECK(!wait_id_);
}

void HandleWaiter::Cancel() {
  if (wait_id_ && handle_) {
    // Hold a reference to this object.
    fxl::RefPtr<HandleWaiter> ref(this);

    // Cancel the wait and clear wait_id_.
    waiter_->CancelWait(wait_id_);
    wait_id_ = 0;

    // Release this object from the handle and clear handle_.
    handle_->ReleaseWaiter(this);
    handle_ = nullptr;
  }
  FXL_DCHECK(handle_ == nullptr);
  FXL_DCHECK(wait_id_ == 0);
}

void HandleWaiter::OnWaitComplete(mx_status_t status, mx_signals_t pending) {
  FXL_DCHECK(wait_id_);
  FXL_DCHECK(handle_);

  FXL_DCHECK(!callback_.is_empty());
  FXL_DCHECK(callback_.dart_state());

  // Hold a reference to this object.
  fxl::RefPtr<HandleWaiter> ref(this);

  // Ask the handle to release this waiter.
  handle_->ReleaseWaiter(this);

  // Clear handle_ and wait_id_.
  handle_ = nullptr;
  wait_id_ = 0;

  DartState::Scope scope(callback_.dart_state().get());

  std::vector<Dart_Handle> args{ToDart(status), ToDart(pending)};
  tonic::LogIfError(
      Dart_InvokeClosure(callback_.Release(), args.size(), args.data()));
}

void HandleWaiter::CallOnWaitComplete(mx_status_t status,
                                      mx_signals_t pending,
                                      uint64_t count,
                                      void* closure) {
  HandleWaiter* handle_waiter = static_cast<HandleWaiter*>(closure);
  // TODO: plumb count through to Dart.
  handle_waiter->OnWaitComplete(status, pending);
}

}  // namespace dart
}  // namespace zircon
