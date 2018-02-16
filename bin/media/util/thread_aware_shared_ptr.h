// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fxl/tasks/task_runner.h"

namespace media {

template <typename T>
struct ThreadAwareDeleter {
  ThreadAwareDeleter(fxl::RefPtr<fxl::TaskRunner> task_runner)
      : task_runner_(task_runner) {}

  void operator()(T* t) {
    if (task_runner_->RunsTasksOnCurrentThread()) {
      delete t;
    } else {
      task_runner_->PostTask([t]() { delete t; });
    }
  }

 private:
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
};

template <typename T>
std::shared_ptr<T> ThreadAwareSharedPtr(
    T* t,
    fxl::RefPtr<fxl::TaskRunner> task_runner) {
  return std::shared_ptr<T>(t, ThreadAwareDeleter<T>(task_runner));
}

}  // namespace media
