// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/weak_stub_controller.h"

namespace fidl {
namespace internal {

WeakStubController::WeakStubController(StubController* controller)
    : ref_count_(1u),
      controller_(controller),
#if ZX_DEBUG_ASSERT_IMPLEMENTED
      thread_(thrd_current())
#else
      thread_(thrd_t{})
#endif
{
#if !ZX_DEBUG_ASSERT_IMPLEMENTED
  // convince the compiler that thread_ is "used" in release
  (void)thread_;
#endif
}

WeakStubController::~WeakStubController() = default;

void WeakStubController::AddRef() {
  ZX_DEBUG_ASSERT_COND(IsCurrentThreadOk());
  ZX_DEBUG_ASSERT(ref_count_ != 0);
  ++ref_count_;
}

void WeakStubController::Release() {
  // We have to allow !controller_ here, due to async::Loop::Shutdown() calling Invalidate(),
  // Release() from a thread other than async::Loop::StartThread(), after the
  // async::Loop::StartThread() thread has been joined.
  ZX_DEBUG_ASSERT_COND(IsCurrentThreadOk() || !controller_);
  ZX_DEBUG_ASSERT(ref_count_ > 0);
  if (--ref_count_ == 0)
    delete this;
}

void WeakStubController::Invalidate() {
  // This call can occur during async::Loop::Shutdown(), from a thread other than the thread created
  // by async::Loop::StartThread().  In that usage path, it's typically ok, and in this method we
  // don't attempt to detect cases that are not ok.  In correct usage, the
  // async::Loop::StartThread() thread has been joined, so that thread can't be touching
  // WeakStubController.
  controller_ = nullptr;
}

StubController* WeakStubController::controller() const {
  ZX_DEBUG_ASSERT_COND(IsCurrentThreadOk());
  return controller_;
}

#if ZX_DEBUG_ASSERT_IMPLEMENTED
bool WeakStubController::IsCurrentThreadOk() const {
  // The check for thrd_t{} will always be false, unless the release build constructor ran somehow
  // despite the present method being debug-only.  The extra check is to avoid asserting if the
  // release constructor was used followed by debug methods.  That's not expected to be common, but
  // avoid asserting in mixed debug/release builds like that, just in case.
  return (thrd_current() == thread_) || (thread_ == thrd_t{});
}
#endif

}  // namespace internal
}  // namespace fidl
