// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/process.h>
#include <zircon/threads.h>

#include <thread>
#include <type_traits>

#include <runtime/thread.h>

#include "threads_impl.h"

namespace {

pthread* ThreadStruct(thrd_t t) { return reinterpret_cast<pthread*>(t); }

zxr_thread_t* ZxrThread(thrd_t t) { return &ThreadStruct(t)->zxr_thread; }

zx_handle_t GetHandle(thrd_t t) { return zxr_thread_get_handle(ZxrThread(t)); }

}  // namespace

__EXPORT zx_handle_t thrd_get_zx_handle(thrd_t t) { return GetHandle(t); }

__EXPORT zx_handle_t native_thread_get_zx_handle(std::thread::native_handle_type t) {
  static_assert(std::is_same_v<std::thread::native_handle_type, thrd_t>);
  return GetHandle(t);
}

__EXPORT zx_handle_t _zx_thread_self() {
  return zxr_thread_get_handle(&__pthread_self()->zxr_thread);
}
__EXPORT decltype(zx_thread_self) zx_thread_self [[gnu::weak, gnu::alias("_zx_thread_self")]];
