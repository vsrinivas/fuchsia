// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "integration-test.h"

namespace libdriver_integration_test {

class BasicLifecycleTest : public IntegrationTest {};

// This test checks what happens when a driver returns an error from bind.
TEST_F(BasicLifecycleTest, BindError) {
  std::unique_ptr<RootMockDevice> root_mock_device;

  auto promise =
      ExpectBind(&root_mock_device, [](HookInvocation record, Completer<void> completer) {
        completer.complete_ok();
        ActionList actions;
        actions.AppendReturnStatus(ZX_ERR_NOT_SUPPORTED);
        return actions;
      });
  RunPromise(std::move(promise));
}

// This test confirms that after a device has been added:
// 1) When it's parent is removed, the device receives its unbind() callback.
// 2) If the device calls device_remove() in the unbind() callback, its
//    release() callback gets called later.
TEST_F(BasicLifecycleTest, BindThenUnbindAndRemove) {
  std::unique_ptr<RootMockDevice> root_mock_device;
  std::unique_ptr<MockDevice> mock_child_device;

  auto promise =
      CreateFirstChild(&root_mock_device, &mock_child_device).and_then([&]() -> Promise<void> {
        // Destroy the test device.  This should cause an unbind of the child
        // device.
        root_mock_device.reset();
        return ExpectUnbindThenRelease(mock_child_device);
      });

  RunPromise(std::move(promise));
}

// This test confirms that after a device has been added:
// 1) We can open it via devfs, and its open() hook gets called.
// 2) We can remove the device via device_async_remove() and its unbind() hook gets called
// 3) We can close the opened connection, and its close() and then release() hook gets called.
TEST_F(BasicLifecycleTest, BindThenOpenRemoveAndClose) {
  std::unique_ptr<RootMockDevice> root_mock_device;
  std::unique_ptr<MockDevice> mock_child_device;
  fidl::InterfacePtr<fuchsia::io::Node> client;

  auto promise =
      CreateFirstChild(&root_mock_device, &mock_child_device)
          .and_then([&]() {
            // Do the open and wait for acknowledgement that it was successful.
            auto wait_for_open = DoOpen(mock_child_device->path(), &client);
            auto expect_open =
                ExpectOpen(mock_child_device,
                           [](HookInvocation record, uint32_t flags, Completer<void> completer) {
                             completer.complete_ok();
                             ActionList actions;
                             // Request the child device be removed.
                             actions.AppendAsyncRemoveDevice();
                             actions.AppendReturnStatus(ZX_OK);
                             return actions;
                           });
            auto expect_unbind =
                ExpectUnbind(mock_child_device,
                             [](HookInvocation record, Completer<void> completer) {
                               ActionList actions;
                               actions.AppendUnbindReply(std::move(completer));
                               return actions;
                             });
            return expect_open.and_then(std::move(expect_unbind))
                .and_then(std::move(wait_for_open));
          })
          .and_then([&]() {
            // Close the newly opened connection
            client.Unbind();
            return ExpectClose(mock_child_device, [](HookInvocation record,
                                                     uint32_t flags, Completer<void> completer) {
              completer.complete_ok();
              ActionList actions;
              actions.AppendReturnStatus(ZX_OK);
              return actions;
            });
          })
          .and_then([&]() -> Promise<void> {
            // Since DdkAsyncRemove() has been called and all connections have been closed,
            // the device should be released.
            return ExpectRelease(mock_child_device);
          });

  RunPromise(std::move(promise));
}

// This test confirms that after a device has been added:
// 1) We can open it via devfs, and its open() hook gets called.
// 2) We can close the opened connection, and its close() hook gets called.
// 3) Invoking device_remove causes the release hook to run.
TEST_F(BasicLifecycleTest, BindThenOpenCloseAndRemove) {
  std::unique_ptr<RootMockDevice> root_mock_device;
  std::unique_ptr<MockDevice> mock_child_device;
  fidl::InterfacePtr<fuchsia::io::Node> client;

  auto promise =
      CreateFirstChild(&root_mock_device, &mock_child_device)
          .and_then([&]() {
            // Do the open and wait for acknowledgement that it was successful.
            auto wait_for_open = DoOpen(mock_child_device->path(), &client);
            auto expect_open =
                ExpectOpen(mock_child_device,
                           [](HookInvocation record, uint32_t flags, Completer<void> completer) {
                             completer.complete_ok();
                             ActionList actions;
                             actions.AppendReturnStatus(ZX_OK);
                             return actions;
                           });
            return expect_open.and_then(std::move(wait_for_open));
          })
          .and_then([&]() {
            // Close the newly opened connection
            client.Unbind();
            return ExpectClose(mock_child_device, [](HookInvocation record, uint32_t flags,
                                                     Completer<void> completer) {
              completer.complete_ok();
              ActionList actions;
              actions.AppendReturnStatus(ZX_OK);
              return actions;
            });
          })
          .and_then([&]() -> Promise<void> {
            // Destroy the test device.  This should cause an unbind of the child
            // device.
            root_mock_device.reset();
            return ExpectUnbindThenRelease(mock_child_device);
          });

  RunPromise(std::move(promise));
}

// This test confirms that after a device has been added and opened, it won't be
// released until after its been closed.
TEST_F(BasicLifecycleTest, BindThenOpenRemoveThenClose) {
  std::unique_ptr<RootMockDevice> root_mock_device;
  std::unique_ptr<MockDevice> mock_child_device;
  fidl::InterfacePtr<fuchsia::io::Node> client;

  auto promise =
      CreateFirstChild(&root_mock_device, &mock_child_device)
          .and_then([&]() {
            // Do the open and wait for acknowledgement that it was successful.
            auto wait_for_open = DoOpen(mock_child_device->path(), &client);
            auto expect_open =
                ExpectOpen(mock_child_device,
                           [](HookInvocation record, uint32_t flags, Completer<void> completer) {
                             completer.complete_ok();
                             ActionList actions;
                             actions.AppendReturnStatus(ZX_OK);
                             return actions;
                           });
            return expect_open.and_then(std::move(wait_for_open));
          })
          .and_then([&]() -> Promise<void> {
            // Destroy the test device.  This should cause an unbind of the child
            // device.
            root_mock_device.reset();
            return ExpectUnbind(mock_child_device,
                                [](HookInvocation record, Completer<void> completer) {
                                  ActionList actions;
                                  actions.AppendUnbindReply(std::move(completer));
                                  return actions;
                                });
          })
          .and_then([&]() {
            // Close the newly opened connection.  Release shouldn't be able to
            // happen until then.
            client.Unbind();
            return ExpectClose(mock_child_device, [](HookInvocation record, uint32_t flags,
                                                     Completer<void> completer) {
              completer.complete_ok();
              ActionList actions;
              actions.AppendReturnStatus(ZX_OK);
              return actions;
            });
          })
          .and_then(ExpectRelease(mock_child_device));

  RunPromise(std::move(promise));
}

}  // namespace libdriver_integration_test
