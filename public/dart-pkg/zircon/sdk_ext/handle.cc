// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dart-pkg/zircon/sdk_ext/handle.h"

#include <algorithm>

#include "lib/tonic/dart_binding_macros.h"
#include "lib/tonic/dart_class_library.h"

using tonic::ToDart;

namespace zircon {
namespace dart {

IMPLEMENT_WRAPPERTYPEINFO(zircon, Handle);

Handle::Handle(mx_handle_t handle) : handle_(handle) {}

Handle::~Handle() {
  if (is_valid()) {
    mx_status_t status = Close();
    FTL_DCHECK(status == MX_OK);
  }
}

ftl::RefPtr<Handle> Handle::Create(mx_handle_t handle) {
  return ftl::MakeRefCounted<Handle>(handle);
}

Dart_Handle Handle::CreateInvalid() {
  return ToDart(Create(MX_HANDLE_INVALID));
}

mx_handle_t Handle::ReleaseHandle() {
  FTL_DCHECK(is_valid());

  mx_handle_t handle = handle_;
  handle_ = MX_HANDLE_INVALID;
  while (waiters_.size()) {
    // HandleWaiter::Cancel calls Handle::ReleaseWaiter which removes the
    // HandleWaiter from waiters_.
    waiters_.back()->Cancel();
  }

  FTL_DCHECK(!is_valid());

  return handle;
}

mx_status_t Handle::Close() {
  if (is_valid()) {
    mx_handle_t handle = ReleaseHandle();
    return mx_handle_close(handle);
  }
  return MX_ERR_BAD_HANDLE;
}

ftl::RefPtr<HandleWaiter> Handle::AsyncWait(mx_signals_t signals,
                                            Dart_Handle callback) {
  if (!is_valid()) {
    FTL_LOG(WARNING) << "Attempt to wait on an invalid handle.";
    return nullptr;
  }

  ftl::RefPtr<HandleWaiter> waiter =
      HandleWaiter::Create(this, signals, callback);
  waiters_.push_back(waiter.get());

  return waiter;
}

void Handle::ReleaseWaiter(HandleWaiter* waiter) {
  FTL_DCHECK(waiter);
  auto iter = std::find(waiters_.cbegin(), waiters_.cend(), waiter);
  FTL_DCHECK(iter != waiters_.cend());
  FTL_DCHECK(*iter == waiter);
  waiters_.erase(iter);
}


  // clang-format: off

#define FOR_EACH_STATIC_BINDING(V) \
  V(Handle, CreateInvalid)

#define FOR_EACH_BINDING(V) \
  V(Handle, handle)         \
  V(Handle, is_valid)       \
  V(Handle, Close)          \
  V(Handle, AsyncWait)

// clang-format: on

// Tonic is missing a comma.
#define DART_REGISTER_NATIVE_STATIC_(CLASS, METHOD) \
  DART_REGISTER_NATIVE_STATIC(CLASS, METHOD),

FOR_EACH_STATIC_BINDING(DART_NATIVE_CALLBACK_STATIC)
FOR_EACH_BINDING(DART_NATIVE_CALLBACK)

void Handle::RegisterNatives(tonic::DartLibraryNatives* natives) {
  natives->Register({FOR_EACH_STATIC_BINDING(DART_REGISTER_NATIVE_STATIC_)
                         FOR_EACH_BINDING(DART_REGISTER_NATIVE)});
}

}  // namespace dart
}  // namespace zircon
