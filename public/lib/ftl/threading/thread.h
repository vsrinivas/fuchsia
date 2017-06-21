// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_THREADING_THREAD_H_
#define LIB_FTL_THREADING_THREAD_H_

#include "lib/ftl/build_config.h"

#if defined(OS_WIN)
#include <windows.h>
#else
#include <pthread.h>
#endif

#include <functional>

#include "lib/ftl/ftl_export.h"
#include "lib/ftl/macros.h"

namespace ftl {

class FTL_EXPORT Thread {
 public:
  static constexpr size_t default_stack_size = 1 * 1024 * 1024;

  explicit Thread(std::function<void(void)> runnable);
  ~Thread();
  bool Run(size_t stack_size = default_stack_size);
  bool IsRunning() const;
  bool Join();

 private:
  static void* Entry(void* context);
  void Main();

  std::function<void(void)> runnable_;
#if defined(OS_WIN)
  HANDLE thread_;
#else
  pthread_t thread_;
#endif
  bool running_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Thread);
};

}  // namespace ftl

#endif  // LIB_FTL_THREADING_THREAD_H_
