// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_TEST_FIXTURE_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_TEST_FIXTURE_H_

#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include <deque>
#include <initializer_list>
#include <optional>
#include <unordered_map>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace media::audio::test {

// TestFixture wraps a RealLoopFixture with methods to check for FIDL errors and callbacks.
// For example, to check for disconnection:
//
//     SomeInterfacePtr ptr;
//     environment->ConnectToService(ptr.NewRequest());
//     AddErrorHandler(ptr, "SomeInterface");
//
//     ... do something that should disconnect ptr ...
//
//     ExpectDisconnect(ptr);
//
// Or, to check that a sequence of callbacks are executed as expected:
//
//     SomeInterfacePtr ptr;
//     environment->ConnectToService(ptr.NewRequest());
//     AddErrorHandler(ptr, "SomeInterface");
//
//     int b;
//     ptr.events().OnA = AddCallback("A")
//     ptr.events().OnB = AddCallback("B", [&b](int x) { b = x; });
//
//     // This verifies that callbacks A and B are executed, in that order, that B
//     // is called with the correct argument, and that the ErrorHandler is not called.
//     ExpectCallbacks();
//     EXPECT_EQ(b, 42);
//
class TestFixture : public ::gtest::RealLoopFixture {
 public:
  struct ErrorHandler {
    std::string name;
    zx_status_t error_code = ZX_OK;           // set after the ErrorHandler is triggered
    zx_status_t expected_error_code = ZX_OK;  // expected error for ExpectErrors
  };

  // Add a new ErrorHandler for the given protocol. If this ErrorHandler triggers unexpectedly,
  // the given name will be included in the test failure message. The InterfacePtr must
  // live for the duration of this TestFixture.
  template <class T>
  void AddErrorHandler(fidl::InterfacePtr<T>& ptr, std::string name) {
    auto [h, cb] = NewErrorHandler(name);
    ptr.set_error_handler(std::move(cb));
    error_handlers_[ptr.channel().get()] = h;
  }
  template <class T>
  void AddErrorHandler(fidl::Binding<T>& binding, std::string name) {
    auto [h, cb] = NewErrorHandler(name);
    binding.set_error_handler(std::move(cb));
    error_handlers_[binding.channel().get()] = h;
  }

  // Retrieves a previously-added error handler.
  // Useful for direct calls to ExpectErrors or ExpectDisconnects. Tests that
  // use ExpectError or ExpectDisconnect won't need this.
  template <class T>
  std::shared_ptr<ErrorHandler> ErrorHandlerFor(fidl::InterfacePtr<T>& ptr) {
    auto eh = error_handlers_[ptr.channel().get()];
    FX_CHECK(eh);
    return eh;
  }
  template <class T>
  std::shared_ptr<ErrorHandler> ErrorHandlerFor(fidl::Binding<T>& binding) {
    auto eh = error_handlers_[binding.channel().get()];
    FX_CHECK(eh);
    return eh;
  }

  // Add an expected callback to the pending set.
  // Callbacks are expected to occur in the order in which they are added.
  // Optionally, provide a custom function to invoke when the expected callback is triggered.
  auto AddCallback(const std::string& name);
  template <typename Callable>
  auto AddCallback(const std::string& name, Callable callback);

  // Like AddCallback, but allow the callback to happen in any order.
  auto AddCallbackUnordered(const std::string& name);
  template <typename Callable>
  auto AddCallbackUnordered(const std::string& name, Callable callback);

  // Add an unexpected callback. The test will fail if this callback is triggered.
  auto AddUnexpectedCallback(const std::string& name) {
    return [name](auto&&...) { ADD_FAILURE() << "Got unexpected callback " << name; };
  }

  // Wait until all pending callbacks are drained. Fails if an error is encountered.
  // Callbacks are expected to occur in the order they are added. After this method
  // returns, the pending callback set is emptied and new callbacks may be added for
  // a future call to ExpectCallbacks().
  void ExpectCallbacks();

  // Run loop with specified timeout, expecting to reach the timeout. Fails if an error is
  // encountered, with `msg_for_failure`. The callbacks themselves should include failures
  // such that if they trigger, they register as unexpected errors. After this method returns,
  // the pending callback set is emptied and new callbacks may be added for a future call to
  // ExpectCallback or ExpectNoCallbacks.
  void ExpectNoCallbacks(zx::duration timeout, const std::string& msg_for_failure);

  // Wait for the given ErrorHandlers to trigger with their expected errors. Fails if
  // different errors are found or if errors are triggered in different ErrorHandlers.
  void ExpectErrors(const std::vector<std::shared_ptr<ErrorHandler>>& errors);

  // Shorthand to expect many disconnect errors.
  void ExpectDisconnects(const std::vector<std::shared_ptr<ErrorHandler>>& errors) {
    std::vector<std::shared_ptr<ErrorHandler>> handlers;
    for (auto eh : errors) {
      eh->expected_error_code = ZX_ERR_PEER_CLOSED;
    }
    ExpectErrors(errors);
  }

  // Shorthand to expect a single error.
  template <class T>
  void ExpectError(fidl::InterfacePtr<T>& ptr, zx_status_t expected_error) {
    auto eh = ErrorHandlerFor(ptr);
    eh->expected_error_code = expected_error;
    ExpectErrors({eh});
  }
  template <class T>
  void ExpectDisconnect(fidl::InterfacePtr<T>& ptr) {
    ExpectError(ptr, ZX_ERR_PEER_CLOSED);
  }

  // Verifies that no unexpected errors have occurred so far.
  void ExpectNoUnexpectedErrors(const std::string& msg_for_failure);

  // Reports whether any ErrorHandlers have triggered.
  bool ErrorOccurred();

  // Override this method to crash if the condition is not reached within 1 minute.
  // This helps debug test flakes that surface as deadlocks. New tests should use
  // RunLoopWithTimeoutOrUntil instead of this method.
  void RunLoopUntil(fit::function<bool()> condition, zx::duration step = zx::msec(10)) {
    FX_CHECK(RunLoopWithTimeoutOrUntil(std::move(condition), zx::sec(60), step));
  }

  // Promote to public so that non-subclasses can advance through time.
  using ::gtest::RealLoopFixture::RunLoop;
  using ::gtest::RealLoopFixture::RunLoopUntil;
  using ::gtest::RealLoopFixture::RunLoopUntilIdle;
  using ::gtest::RealLoopFixture::RunLoopWithTimeout;
  using ::gtest::RealLoopFixture::RunLoopWithTimeoutOrUntil;

 protected:
  void TearDown() override;

 private:
  struct PendingCallback {
    std::string name;
    int64_t sequence_num = 0;
    bool ordered;
  };

  auto AddCallbackInternal(const std::string& name, bool ordered) {
    auto pb = NewPendingCallback(name, ordered);
    return [this, pb](auto&&...) { pb->sequence_num = next_sequence_num_++; };
  }

  template <typename Callable>
  auto AddCallbackInternal(const std::string& name, Callable callback, bool ordered) {
    auto pb = NewPendingCallback(name, ordered);
    return [this, pb, callback = std::move(callback)](auto&&... args) {
      pb->sequence_num = next_sequence_num_++;
      callback(std::forward<decltype(args)>(args)...);
    };
  }

  std::pair<std::shared_ptr<ErrorHandler>, fit::function<void(zx_status_t)>> NewErrorHandler(
      const std::string& name);
  std::shared_ptr<PendingCallback> NewPendingCallback(const std::string& name, bool ordered);

  void ExpectErrorsInternal(const std::vector<std::shared_ptr<ErrorHandler>>& errors);

  std::unordered_map<zx_handle_t, std::shared_ptr<ErrorHandler>> error_handlers_;
  std::deque<std::shared_ptr<PendingCallback>> pending_callbacks_;
  int64_t next_sequence_num_ = 1;
  bool new_error_ = false;
};

// These must be defined in the header file, because of the auto return type, but
// also must be defined after AddCallbackInternal.
inline auto TestFixture::AddCallback(const std::string& name) {
  return AddCallbackInternal(name, true);
}

template <typename Callable>
inline auto TestFixture::AddCallback(const std::string& name, Callable callback) {
  return AddCallbackInternal(name, callback, true);
}

inline auto TestFixture::AddCallbackUnordered(const std::string& name) {
  return AddCallbackInternal(name, false);
}

template <typename Callable>
inline auto TestFixture::AddCallbackUnordered(const std::string& name, Callable callback) {
  return AddCallbackInternal(name, callback, false);
}

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_TEST_FIXTURE_H_
