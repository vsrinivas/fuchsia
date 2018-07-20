// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_THREADING_THREAD_H_
#define LIB_FXL_THREADING_THREAD_H_

#include "lib/fxl/build_config.h"

#include <pthread.h>
#include <functional>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/macros.h"

namespace fxl {

class FXL_EXPORT Thread {
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
  pthread_t thread_;
  bool running_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Thread);
};

}  // namespace fxl

#endif  // LIB_FXL_THREADING_THREAD_H_
