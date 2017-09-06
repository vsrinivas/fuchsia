// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DART_PKG_ZIRCON_SDK_EXT_HANDLE_H_
#define DART_PKG_ZIRCON_SDK_EXT_HANDLE_H_

#include <magenta/syscalls.h>

#include <vector>

#include "dart/runtime/include/dart_api.h"
#include "dart-pkg/zircon/sdk_ext/handle_waiter.h"
#include "lib/tonic/dart_library_natives.h"
#include "lib/tonic/dart_wrappable.h"
#include "lib/tonic/typed_data/dart_byte_data.h"

namespace zircon {
namespace dart {
/**
 * Handle is the native peer of a Dart object (Handle in dart:zircon)
 * that holds an mx_handle_t. It tracks active waiters on handle too.
 */
class Handle : public ftl::RefCountedThreadSafe<Handle>,
               public tonic::DartWrappable {
  DEFINE_WRAPPERTYPEINFO();
  FRIEND_REF_COUNTED_THREAD_SAFE(Handle);
  FRIEND_MAKE_REF_COUNTED(Handle);

 public:
  ~Handle();

  static void RegisterNatives(tonic::DartLibraryNatives* natives);

  static ftl::RefPtr<Handle> Create(mx_handle_t handle);
  static ftl::RefPtr<Handle> Create(mx::handle handle) {
    return Create(handle.release());
  }

  static ftl::RefPtr<Handle> Unwrap(Dart_Handle handle) {
    return ftl::RefPtr<Handle>(
        tonic::DartConverter<zircon::dart::Handle*>::FromDart(handle));
  }

  static Dart_Handle CreateInvalid();

  mx_handle_t ReleaseHandle();

  bool is_valid() const { return handle_ != MX_HANDLE_INVALID; }

  mx_handle_t handle() const { return handle_; }

  mx_status_t Close();

  ftl::RefPtr<HandleWaiter> AsyncWait(mx_signals_t signals,
                                      Dart_Handle callback);

  void ReleaseWaiter(HandleWaiter* waiter);

 private:
  explicit Handle(mx_handle_t handle);

  mx_handle_t handle_;

  std::vector<HandleWaiter*> waiters_;
};

}  // namespace dart
}  // namespace zircon

#endif  // DART_PKG_ZIRCON_SDK_EXT_HANDLE_H_
