// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/channel.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fidl/cpp/bindings/tests/util/test_waiter.h"
#include "lib/fidl/compiler/interfaces/tests/sample_interfaces.fidl.h"

///////////////////////////////////////////////////////////////////////////////
//
// The tests in this file are designed to test the interaction between a
// std::function and its associated Binding. If a std::function is deleted before
// being used we DCHECK fail--unless the associated Binding has already
// been closed or deleted. This contract must be explained to the Mojo
// application developer. For example it is the developer's responsibility to
// ensure that the Binding is destroyed before an unused std::function is destroyed.
//
///////////////////////////////////////////////////////////////////////////////

namespace fidl {
namespace test {
namespace {

// A callable object that saves the last value it sees via the
// provided int32_t*. Used on the client side.
class ValueSaver {
 public:
  explicit ValueSaver(int32_t* last_value_seen)
      : last_value_seen_(last_value_seen) {}
  void operator()(int32_t x) const { *last_value_seen_ = x; }

 private:
  int32_t* const last_value_seen_;
};

// An implementation of sample::Provider used on the server side.
// It only implements one of the methods: EchoInt().
// All it does is save the values and callbacks it sees.
class InterfaceImpl : public sample::Provider {
 public:
  InterfaceImpl()
      : last_server_value_seen_(0),
        callback_saved_(new std::function<void(int32_t)>()) {}

  ~InterfaceImpl() override {
    if (callback_saved_) {
      delete callback_saved_;
    }
  }

  // Run's the callback previously saved from the last invocation
  // of |EchoInt()|.
  bool RunCallback() {
    if (callback_saved_) {
      (*callback_saved_)(last_server_value_seen_);
      return true;
    }
    return false;
  }

  // Delete's the previously saved callback.
  void DeleteCallback() {
    delete callback_saved_;
    callback_saved_ = nullptr;
  }

  // sample::Provider implementation

  // Saves its two input values in member variables and does nothing else.
  void EchoInt(int32_t x, const std::function<void(int32_t)>& callback) override {
    last_server_value_seen_ = x;
    *callback_saved_ = callback;
  }

  void EchoString(const String& a,
                  const std::function<void(String)>& callback) override {
    FXL_CHECK(false) << "Not implemented.";
  }

  void EchoStrings(const String& a,
                   const String& b,
                   const std::function<void(String, String)>& callback) override {
    FXL_CHECK(false) << "Not implemented.";
  }

  void EchoMessagePipeHandle(
      mx::channel a,
      const std::function<void(mx::channel)>& callback) override {
    FXL_CHECK(false) << "Not implemented.";
  }

  void EchoEnum(sample::Enum a,
                const std::function<void(sample::Enum)>& callback) override {
    FXL_CHECK(false) << "Not implemented.";
  }

  void resetLastServerValueSeen() { last_server_value_seen_ = 0; }

  int32_t last_server_value_seen() const { return last_server_value_seen_; }

 private:
  int32_t last_server_value_seen_;
  std::function<void(int32_t)>* callback_saved_;
};

class BindingCallbackTest : public testing::Test {
 public:
  ~BindingCallbackTest() override {}

 protected:
  int32_t last_client_callback_value_seen_;
  sample::ProviderPtr interface_ptr_;

  void PumpMessages() { WaitForAsyncWaiter(); }
};

// Tests that the InterfacePtr and the Binding can communicate with each
// other normally.
TEST_F(BindingCallbackTest, Basic) {
  // Create the ServerImpl and the Binding.
  InterfaceImpl server_impl;
  Binding<sample::Provider> binding(&server_impl, interface_ptr_.NewRequest());

  // Initialize the test values.
  server_impl.resetLastServerValueSeen();
  last_client_callback_value_seen_ = 0;

  // Invoke the Echo method.
  interface_ptr_->EchoInt(7, ValueSaver(&last_client_callback_value_seen_));
  PumpMessages();

  // Check that server saw the correct value, but the client has not yet.
  EXPECT_EQ(7, server_impl.last_server_value_seen());
  EXPECT_EQ(0, last_client_callback_value_seen_);

  // Now run the std::function.
  server_impl.RunCallback();
  PumpMessages();

  // Check that the client has now seen the correct value.
  EXPECT_EQ(7, last_client_callback_value_seen_);

  // Initialize the test values again.
  server_impl.resetLastServerValueSeen();
  last_client_callback_value_seen_ = 0;

  // Invoke the Echo method again.
  interface_ptr_->EchoInt(13, ValueSaver(&last_client_callback_value_seen_));
  PumpMessages();

  // Check that server saw the correct value, but the client has not yet.
  EXPECT_EQ(13, server_impl.last_server_value_seen());
  EXPECT_EQ(0, last_client_callback_value_seen_);

  // Now run the Callback again.
  server_impl.RunCallback();
  PumpMessages();

  // Check that the client has now seen the correct value again.
  EXPECT_EQ(13, last_client_callback_value_seen_);
}

// Tests that running the Callback after the Binding has been deleted
// results in a clean failure.
TEST_F(BindingCallbackTest, DeleteBindingThenRunCallback) {
  // Create the ServerImpl.
  InterfaceImpl server_impl;
  {
    // Create the binding in an inner scope so it can be deleted first.
    Binding<sample::Provider> binding(&server_impl, interface_ptr_.NewRequest());

    // Initialize the test values.
    server_impl.resetLastServerValueSeen();
    last_client_callback_value_seen_ = 0;

    // Invoke the Echo method.
    interface_ptr_->EchoInt(7, ValueSaver(&last_client_callback_value_seen_));
    PumpMessages();
  }
  // The binding has now been destroyed and the pipe is closed.

  // Check that server saw the correct value, but the client has not yet.
  EXPECT_EQ(7, server_impl.last_server_value_seen());
  EXPECT_EQ(0, last_client_callback_value_seen_);

  // Now try to run the Callback. This should do nothing since the pipe
  // is closed.
  EXPECT_TRUE(server_impl.RunCallback());
  PumpMessages();

  // Check that the client has still not seen the correct value.
  EXPECT_EQ(0, last_client_callback_value_seen_);

  // Attempt to invoke the method again and confirm that an error was
  // encountered.
  interface_ptr_->EchoInt(13, ValueSaver(&last_client_callback_value_seen_));
  PumpMessages();
  EXPECT_TRUE(interface_ptr_.encountered_error());
}

// Tests that deleting a Callback without running it after the corresponding
// binding has already been deleted does not result in a crash.
TEST_F(BindingCallbackTest, DeleteBindingThenDeleteCallback) {
  // Create the ServerImpl.
  InterfaceImpl server_impl;
  {
    // Create the binding in an inner scope so it can be deleted first.
    Binding<sample::Provider> binding(&server_impl, interface_ptr_.NewRequest());

    // Initialize the test values.
    server_impl.resetLastServerValueSeen();
    last_client_callback_value_seen_ = 0;

    // Invoke the Echo method.
    interface_ptr_->EchoInt(7, ValueSaver(&last_client_callback_value_seen_));
    PumpMessages();
  }
  // The binding has now been destroyed and the pipe is closed.

  // Check that server saw the correct value, but the client has not yet.
  EXPECT_EQ(7, server_impl.last_server_value_seen());
  EXPECT_EQ(0, last_client_callback_value_seen_);

  // Delete the callback without running it. This should not
  // cause a problem because the insfrastructure can detect that the
  // binding has already been destroyed and the pipe is closed.
  server_impl.DeleteCallback();
}

// Tests that closing a Binding allows us to delete a callback
// without running it without encountering a crash.
TEST_F(BindingCallbackTest, CloseBindingBeforeDeletingCallback) {
  // Create the ServerImpl and the Binding.
  InterfaceImpl server_impl;
  Binding<sample::Provider> binding(&server_impl, interface_ptr_.NewRequest());

  // Initialize the test values.
  server_impl.resetLastServerValueSeen();
  last_client_callback_value_seen_ = 0;

  // Invoke the Echo method.
  interface_ptr_->EchoInt(7, ValueSaver(&last_client_callback_value_seen_));
  PumpMessages();

  // Check that server saw the correct value, but the client has not yet.
  EXPECT_EQ(7, server_impl.last_server_value_seen());
  EXPECT_EQ(0, last_client_callback_value_seen_);

  // Now close the Binding.
  binding.Close();

  // Delete the callback without running it. This should not
  // cause a crash because the insfrastructure can detect that the
  // binding has already been closed.
  server_impl.DeleteCallback();

  // Check that the client has still not seen the correct value.
  EXPECT_EQ(0, last_client_callback_value_seen_);
}

// Tests that deleting a Callback without using it before the
// Binding has been destroyed or closed results in a DCHECK.
TEST_F(BindingCallbackTest, DeleteCallbackBeforeBindingDeathTest) {
  // Create the ServerImpl and the Binding.
  InterfaceImpl server_impl;
  Binding<sample::Provider> binding(&server_impl, interface_ptr_.NewRequest());

  // Initialize the test values.
  server_impl.resetLastServerValueSeen();
  last_client_callback_value_seen_ = 0;

  // Invoke the Echo method.
  interface_ptr_->EchoInt(7, ValueSaver(&last_client_callback_value_seen_));
  PumpMessages();

  // Check that server saw the correct value, but the client has not yet.
  EXPECT_EQ(7, server_impl.last_server_value_seen());
  EXPECT_EQ(0, last_client_callback_value_seen_);

#if !defined(NDEBUG) || defined(DCHECK_ALWAYS_ON)
  // Delete the callback without running it. This should cause a crash in debug
  // builds due to a DCHECK.
  EXPECT_DEATH_IF_SUPPORTED(server_impl.DeleteCallback(),
                            "Check failed: !callback_was_dropped.");
#endif
}

}  // namespace
}  // namespace test
}  // namespace fidl
