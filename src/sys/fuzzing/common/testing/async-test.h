// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTING_ASYNC_TEST_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_ASYNC_TEST_H_

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/async-types.h"
#include "testing/fidl/async_loop_for_test.h"

namespace fuzzing {

// This class acts as a base class for various unit tests. It extends gTest's |Test| class
// by providing an async loop for testing with an |async::Executor| has been set up.
class AsyncTest : public ::testing::Test {
 protected:
  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }
  const ExecutorPtr& executor() const { return executor_; }
  size_t active() const { return active_; }

  void SetUp() override { executor_ = MakeExecutor(dispatcher()); }

// Each of these macros creates a promise from a provided handler with additional source location
// information. The promise expects the indicated kind of result.
//
// Example usage:
//   auto f = FUZZING_EXPECT_OK([&] { return AwaitSomething(); });
//   auto g = FUZZING_EXPECT_ERROR([&] { return AwaitFailure(); }, ZX_ERR_BAD_STATE);
//
#define FUZZING_EXPECT_OK(...) Schedule(ExpectOk(__FILE__, __LINE__, __VA_ARGS__))
#define FUZZING_EXPECT_ERROR(...) Schedule(ExpectError(__FILE__, __LINE__, __VA_ARGS__))

  // Runs a promise using this object's test loop.
  template <typename Handler>
  void Schedule(Handler handler) {
    auto promise = fpromise::make_promise(std::move(handler));
    auto wrapper =
        promise.inspect([this](typename decltype(promise)::result_type& result) { --active_; });
    ++active_;
    executor_->schedule_task(std::move(wrapper));
  }

  // Checks if a promise returns ok. |handler| can be anything that can be passed to
  // |fpromise::make_promise| to make a promise.
  //
  // Callers should use |EXPECT_OK| instead of calling this directly.
  //
  template <typename Handler>
  Promise<> ExpectOk(const char* file, int line, Handler&& handler) {
    auto promise = fpromise::make_promise(std::move(handler));
    return promise
        .inspect([file, line](typename decltype(promise)::result_type& result) {
          EXPECT_TRUE(result.is_ok()) << "Called from " << file << ":" << line;
        })
        .discard_result();
  }

  // Checks if a promise returns an|expected| value. |handler| can be anything that can be passed to
  // |fpromise::make_promise| to make a promise.
  //
  // Callers should use |EXPECT_OK| instead of calling this directly.
  //
  template <typename Handler, typename ValueType>
  Promise<> ExpectOk(const char* file, int line, Handler&& handler, ValueType expected) {
    auto promise = fpromise::make_promise(std::move(handler));
    return promise
        .inspect([file, line,
                  expected = std::move(expected)](typename decltype(promise)::result_type& result) {
          ASSERT_TRUE(result.is_ok()) << "Called from " << file << ":" << line;
          EXPECT_EQ(result.value(), expected) << "Called from " << file << ":" << line;
        })
        .discard_result();
  }

  // Checks if a promise returns ok and returns its value via |out|. |handler| can be anything that
  // can be passed to |fpromise::make_promise| to make a promise.
  //
  // Callers should use |EXPECT_OK| instead of calling this directly.
  //
  template <typename Handler, typename ValueType>
  Promise<> ExpectOk(const char* file, int line, Handler&& handler, ValueType* out) {
    auto promise = fpromise::make_promise(std::move(handler));
    return promise.then(
        [file, line, out](typename decltype(promise)::result_type& result) -> Result<> {
          EXPECT_TRUE(result.is_ok()) << "Called from " << file << ":" << line;
          if (result.is_ok()) {
            *out = result.take_value();
          }
          return fpromise::ok();
        });
  }

  // Checks if a promise returns an error. |handler| can be anything that can be passed to
  // |fpromise::make_promise| to make a promise.
  //
  // Callers should use |EXPECT_ERROR| instead of calling this directly.
  //
  template <typename Handler>
  Promise<> ExpectError(const char* file, int line, Handler&& handler) {
    auto promise = fpromise::make_promise(std::move(handler));
    return promise
        .inspect([file, line](typename decltype(promise)::result_type& result) {
          EXPECT_TRUE(result.is_error()) << "Called from " << file << ":" << line;
        })
        .discard_result();
  }

  // Checks if a promise returns an |expected| error. |handler| can be anything that can be passed
  // to |fpromise::make_promise| to make a promise.
  //
  // Callers should use |EXPECT_ERROR| instead of calling this directly.
  //
  template <typename Handler, typename ErrorType>
  Promise<> ExpectError(const char* file, int line, Handler&& handler, ErrorType expected) {
    auto promise = fpromise::make_promise(std::move(handler));
    return promise
        .inspect([file, line,
                  expected = std::move(expected)](typename decltype(promise)::result_type& result) {
          ASSERT_TRUE(result.is_error()) << "Called from " << file << ":" << line;
          EXPECT_EQ(result.error(), expected) << "Called from " << file << ":" << line;
        })
        .discard_result();
  }

  // Runs the test async loop.
  void RunUntilIdle() {
    // TODO(fxbug.dev/92490): This is a transitional implementation to be used while the code has
    // multiple dispatchers. It treats the test loop as not being idle until all its outstanding
    // tasks have completed. This is useful in unit tests where the test loop's dispatcher may be
    // distinct from an existing dispatcher running on a dedicated thread. When the migration to a
    // purely async approach is complete, and the dedicated threads removed, this can become simply
    // |loop_.RunUntilIdle()|, and |RunOnce| can be removed.
    RunOnce();
    while (active_) {
      zx::nanosleep(zx::deadline_after(zx::msec(10)));
      RunOnce();
    }
  }

  void RunOnce() { loop_.RunUntilIdle(); }

  // Checks for unfinished promises at test end.
  void TearDown() override {
    EXPECT_EQ(active_, 0U) << active_ << " unfinished task(s) at tear-down.";
  }

 private:
  fidl::test::AsyncLoopForTest loop_;
  ExecutorPtr executor_;
  size_t active_ = 0;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_ASYNC_TEST_H_
