// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FASYNC_TESTS_TEST_UTILS_H_
#define SRC_LIB_FASYNC_TESTS_TEST_UTILS_H_

#include <lib/fasync/future.h>

namespace fasync {
namespace testing {

class immediate_executor final : public executor {
 public:
  class abort_context : public context {
   public:
    abort_context(immediate_executor& executor) : executor_(executor) {}

    immediate_executor& executor() const override { return executor_; }

    suspended_task suspend_task() override {
      __builtin_abort();
      return suspended_task();
    }

   private:
    immediate_executor& executor_;
  };

  immediate_executor() : context_(*this) {}

  abort_context& context() { return context_; }

  template <typename F, ::fasync::internal::requires_conditions<
                            is_future<F>, cpp17::negation<is_void_future<F>>> = true>
  constexpr future_output_t<F> invoke(F&& future) {
    future_poll_t<F> p = cpp20::invoke(future, context_);
    LIB_FASYNC_UNLIKELY if (p.is_pending()) { __builtin_abort(); }

    return std::move(p).output();
  }

  template <typename F, ::fasync::internal::requires_conditions<is_void_future<F>> = true>
  constexpr void invoke(F&& future) {
    future_poll_t<F> p = cpp20::invoke(future, context_);
    LIB_FASYNC_UNLIKELY if (p.is_pending()) { __builtin_abort(); }
  }

  template <typename F, ::fasync::internal::requires_conditions<is_future<F>> = true>
  constexpr future_poll_t<F> poll(F&& future) {
    return cpp20::invoke(future, context_);
  }

  template <typename F, ::fasync::internal::requires_conditions<is_future<F>> = true>
  constexpr void schedule(F&& future) {
    invoke(std::forward<F>(future));
  }

  void schedule(pending_task&& task) override { schedule(std::move(task).take_future()); }

 private:
  abort_context context_;
};

class invoke_closure final : public ::fasync::internal::future_adaptor_closure<invoke_closure> {
 public:
  template <typename F, ::fasync::internal::requires_conditions<is_future<F>> = true>
  LIB_FASYNC_NODISCARD constexpr auto operator()(F&& future) const {
    return immediate_executor().invoke(std::forward<F>(future));
  }
};

LIB_FASYNC_INLINE_CONSTANT constexpr invoke_closure invoke;

class poll_closure final : public ::fasync::internal::future_adaptor_closure<poll_closure> {
 public:
  template <typename F, ::fasync::internal::requires_conditions<is_future<F>> = true>
  LIB_FASYNC_NODISCARD constexpr auto operator()(F&& future) const {
    return immediate_executor().poll(std::forward<F>(future));
  }
};

LIB_FASYNC_INLINE_CONSTANT constexpr poll_closure poll;

}  // namespace testing
}  // namespace fasync

#endif  // SRC_LIB_FASYNC_TESTS_TEST_UTILS_H_
