// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_LIBDRIVER_INTEGRATION_TEST_INTEGRATION_TEST_H_
#define SRC_DEVICES_TESTS_LIBDRIVER_INTEGRATION_TEST_INTEGRATION_TEST_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fpromise/promise.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>

#include <memory>

#include <gtest/gtest.h>

#include "mock-device-hooks.h"
#include "mock-device.h"
#include "root-mock-device.h"

// Wrapper for an assert that converts a failure to a return of a // fpromise::promise<void, const
// char*> that resolves immediately to a fpromise::error()
#define PROMISE_ASSERT(assertion)                                            \
  do {                                                                       \
    [&]() { assertion; }();                                                  \
    if (testing::Test::HasFatalFailure()) {                                  \
      return fpromise::make_error_promise(std::string("Assertion failure")); \
    }                                                                        \
  } while (0)

// Wrapper for an assert that converts a failure to a return of a fpromise::error()
#define ERROR_ASSERT(assertion)                                 \
  do {                                                          \
    [&]() { assertion; }();                                     \
    if (testing::Test::HasFatalFailure()) {                     \
      return fpromise::error(std::string("Assertion failure")); \
    }                                                           \
  } while (0)

namespace libdriver_integration_test {

class IntegrationTest : public testing::Test {
 public:
  static void SetUpTestSuite();
  static void TearDownTestSuite();

  IntegrationTest();
  ~IntegrationTest() override;

  void SetUp() override;

  using Error = std::string;
  template <class T>
  using Result = fpromise::result<T, Error>;
  template <class T>
  using Promise = fpromise::promise<T, Error>;
  template <class T>
  using Completer = fpromise::completer<T, Error>;
  using HookInvocation = fuchsia::device::mock::HookInvocation;

  // Convenience method on top of ExpectBind for having bind create a child
  // and return success.
  static Promise<void> CreateFirstChild(std::unique_ptr<RootMockDevice>* root_mock_device,
                                        std::unique_ptr<MockDevice>* child_device);

  // Convenience method on top of ExpectUnbind and ExpectRelease for having
  // unbind invoke device_remove(), with the belief that that will drop the
  // last reference to the device and Release() will be called.
  static Promise<void> ExpectUnbindThenRelease(const std::unique_ptr<MockDevice>& device);

  // Initializes |root_mock_device| and returns a promise that will be complete after
  // the root mock device's bind hook has been called.  The bind hook will
  // perform the given |actions|.
  static Promise<void> ExpectBind(std::unique_ptr<RootMockDevice>* root_mock_device,
                                  BindOnce::Callback actions_callback);

  // Returns a promise that will be complete after the device invokes its
  // unbind() hook and performs the given |actions|.  |device| must outlive
  // this promise.
  static Promise<void> ExpectUnbind(const std::unique_ptr<MockDevice>& device,
                                    UnbindOnce::Callback actions_callback);

  // Returns a promise that will be complete after the device invokes its
  // open() hook and performs the given |actions|.  |device| must outlive
  // this promise.
  static Promise<void> ExpectOpen(const std::unique_ptr<MockDevice>& device,
                                  OpenOnce::Callback actions_callback);

  // Returns a promise that will be complete after the device invokes its
  // close() hook and performs the given |actions|.  |device| must outlive
  // this promise.
  static Promise<void> ExpectClose(const std::unique_ptr<MockDevice>& device,
                                   CloseOnce::Callback actions_callback);

  // Returns a promise that will be complete after the device invokes its
  // release() hook. |device| must outive this promise.
  static Promise<void> ExpectRelease(const std::unique_ptr<MockDevice>& device);

  // Performs an open of the given |path| relative to the devfs, and puts the
  // connection into |client|.  The promise returned completes when the open
  // result is sent.  We must setup an open hook handler in order for that
  // promise to be completed.
  Promise<void> DoOpen(const std::string& path, fidl::InterfacePtr<fuchsia::io::Node>* client,
                       uint32_t flags = fuchsia::io::OPEN_RIGHT_READABLE |
                                        fuchsia::io::OPEN_RIGHT_WRITABLE);

  // Waits for the given |path| relative to devfs to be available.  Currently
  // waiting for paths in which non-terminal directories don't yet exist is
  // not supported.
  Promise<void> DoWaitForPath(const std::string& path);

  // Joins two promises and collapses the results such that if either failed
  // the returned promise fails.
  static auto JoinPromises(Promise<void> promise1, Promise<void> promise2) {
    return join_promises(std::move(promise1), std::move(promise2))
        .then([](fpromise::result<std::tuple<Result<void>, Result<void>>>& wrapped_results)
                  -> Result<void> {
          // join_promises() can't fail, so just extract the value
          auto results = wrapped_results.value();
          if (std::get<0>(results).is_error()) {
            return std::get<0>(results);
          }
          return std::get<1>(results);
        });
  }

  // Run the given promise and transform its error case into a test failure.
  static void RunPromise(Promise<void> promise);

 protected:
  static void DoSetup();

  using IsolatedDevmgr = devmgr_integration_test::IsolatedDevmgr;
  static IsolatedDevmgr devmgr_;

  static async::Loop loop_;
  fidl::InterfacePtr<fuchsia::io::Directory> devfs_;
};

}  // namespace libdriver_integration_test

#endif  // SRC_DEVICES_TESTS_LIBDRIVER_INTEGRATION_TEST_INTEGRATION_TEST_H_
