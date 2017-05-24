// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CALLBACK_ASYNCHRONOUS_CALLBACK_H_
#define APPS_LEDGER_SRC_CALLBACK_ASYNCHRONOUS_CALLBACK_H_

#include <tuple>

#include "lib/ftl/functional/apply.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/mtl/tasks/message_loop.h"

namespace callback {

namespace internal {

template <typename T>
class AsynchronousCallback {
 public:
  explicit AsynchronousCallback(ftl::RefPtr<ftl::TaskRunner> task_runner,
                                T func)
      : task_runner_(std::move(task_runner)), func_(std::move(func)) {}

  template <typename... ArgType>
  void operator()(ArgType&&... args) {
    task_runner_->PostTask(ftl::MakeCopyable([
      f = std::move(func_),
      tuple = std::make_tuple(std::forward<ArgType>(args)...)
    ]() mutable { ftl::Apply(std::move(f), std::move(tuple)); }));
  }

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  T func_;
};

}  // namespace internal

template <typename T>
auto MakeAsynchronous(T lambda,
                      ftl::RefPtr<ftl::TaskRunner> task_runner =
                          mtl::MessageLoop::GetCurrent()->task_runner()) {
  return ::callback::internal::AsynchronousCallback<T>(task_runner,
                                                       std::move(lambda));
}

}  // namespace callback

#endif  // APPS_LEDGER_SRC_CALLBACK_ASYNCHRONOUS_CALLBACK_H_
