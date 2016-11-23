// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DART_SDK_EXT_SRC_HANDLE_WAITER_H_
#define LIB_FIDL_DART_SDK_EXT_SRC_HANDLE_WAITER_H_

#include <mx/handle.h>

#include <mutex>

#include "lib/tonic/dart_wrappable.h"
#include "lib/fidl/c/waiter/async_waiter.h"

namespace tonic {
class DartLibraryNatives;
}  // namespace tonic

namespace fidl {
namespace dart {

class HandleWaiter : public ftl::RefCountedThreadSafe<HandleWaiter>,
                     public tonic::DartWrappable {
  DEFINE_WRAPPERTYPEINFO();
  FRIEND_REF_COUNTED_THREAD_SAFE(HandleWaiter);
  FRIEND_MAKE_REF_COUNTED(HandleWaiter);

 public:
  static ftl::RefPtr<HandleWaiter> Create(std::string stack);

  void asyncWait(mx_handle_t handle, mx_signals_t signals, mx_time_t timeout);
  void cancelWait();

  static void RegisterNatives(tonic::DartLibraryNatives* natives);

 private:
  explicit HandleWaiter(std::string stack);
  ~HandleWaiter();

  void OnWaitComplete(mx_status_t status, mx_signals_t pending);
  static void CallOnWaitComplete(mx_status_t status,
                                 mx_signals_t pending,
                                 void* closure);

  const FidlAsyncWaiter* waiter_;
  ftl::WeakPtr<tonic::DartState> dart_state_;

  std::string creation_stack_;
  FidlAsyncWaitID wait_id_ = 0;
};

}  // namespace dart
}  // namespace fidl

#endif  // LIB_FIDL_DART_SDK_EXT_SRC_HANDLE_WAITER_H_
