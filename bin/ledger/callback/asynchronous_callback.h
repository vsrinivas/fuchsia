// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CALLBACK_ASYNCHRONOUS_CALLBACK_H_
#define APPS_LEDGER_SRC_CALLBACK_ASYNCHRONOUS_CALLBACK_H_

#include <tuple>

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
    task_runner_->PostTask(ftl::MakeCopyable(
        AsynchronousClosure<typename std::decay<ArgType>::type...>(
            std::move(func_),
            std::forward<typename std::decay<ArgType>::type>(args)...)));
  }

 private:
  template <typename... Args>
  class AsynchronousClosure {
   public:
    AsynchronousClosure(T function, Args&&... args)
        : function_(std::move(function)),
          params_(std::make_tuple(std::forward<Args>(args)...)) {}

    void operator()() {
      return CallFunction(std::index_sequence_for<Args...>{});
    }

   private:
    template <size_t... S>
    void CallFunction(std::integer_sequence<size_t, S...>) {
      function_(std::get<S>(std::move(params_))...);
    }

    T function_;
    std::tuple<Args...> params_;
  };

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
