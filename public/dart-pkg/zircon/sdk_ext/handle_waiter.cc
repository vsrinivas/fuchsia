// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dart-pkg/zircon/sdk_ext/handle_waiter.h"

#include <async/default.h>

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
                                               zx_signals_t signals,
                                               Dart_Handle callback) {
  return fxl::MakeRefCounted<HandleWaiter>(handle, signals, callback);
}

HandleWaiter::HandleWaiter(Handle* handle,
                           zx_signals_t signals,
                           Dart_Handle callback)
    : wait_(async_get_default(), handle->handle(), signals),
      handle_(handle),
      callback_(DartState::Current(), callback) {
  FXL_CHECK(handle_ != nullptr);
  FXL_CHECK(handle_->is_valid());

  wait_.set_handler([this](async_t* async, zx_status_t status,
                           const zx_packet_signal_t* signal) {
    OnWaitComplete(status, signal->observed);
    return ASYNC_WAIT_FINISHED;
  });
  zx_status_t status = wait_.Begin();
  FXL_DCHECK(status == ZX_OK);
}

HandleWaiter::~HandleWaiter() {
  // Destructor shouldn't be called until it has been released from its
  // Handle.
  FXL_DCHECK(!handle_);
  // Destructor shouldn't be called until the wait has completed or been
  // cancelled.
  FXL_DCHECK(!wait_.is_pending());
}

void HandleWaiter::Cancel() {
  if (wait_.is_pending() && handle_) {
    // Hold a reference to this object.
    fxl::RefPtr<HandleWaiter> ref(this);

    // Cancel the wait and clear wait_id_.
    wait_.Cancel();

    // Release this object from the handle and clear handle_.
    handle_->ReleaseWaiter(this);
    handle_ = nullptr;
  }
  FXL_DCHECK(handle_ == nullptr);
  FXL_DCHECK(!wait_.is_pending());
}

void HandleWaiter::OnWaitComplete(zx_status_t status, zx_signals_t pending) {
  FXL_DCHECK(handle_);

  FXL_DCHECK(!callback_.is_empty());
  FXL_DCHECK(callback_.dart_state());

  // Hold a reference to this object.
  fxl::RefPtr<HandleWaiter> ref(this);

  // Remove this waiter from the handle.
  handle_->ReleaseWaiter(this);

  // Clear handle_.
  handle_ = nullptr;

  DartState::Scope scope(callback_.dart_state().get());

  std::vector<Dart_Handle> args{ToDart(status), ToDart(pending)};
  FXL_DCHECK(!callback_.is_empty());
  tonic::LogIfError(
      Dart_InvokeClosure(callback_.Release(), args.size(), args.data()));
}

}  // namespace dart
}  // namespace zircon
