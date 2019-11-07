// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <ddk/platform-defs.h>
#include <fbl/unique_fd.h>
#include <fuchsia/device/cpp/fidl.h>
#include <fuchsia/device/test/cpp/fidl.h>
#include <gtest/gtest.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "integration-test.h"

namespace libdriver_integration_test {

class CompositeDeviceTest : public IntegrationTest {
 public:
  static void SetUpTestCase() { DoSetup(true /* should_create_composite */); }

 protected:
  // Create the components for the well-known composite that the mock sysdev creates.
  Promise<void> CreateComponentDevices(std::unique_ptr<RootMockDevice>* root_device,
                                       std::unique_ptr<MockDevice>* child1_device,
                                       std::unique_ptr<MockDevice>* child2_device) {
    fit::bridge<void, Error> child1_bridge;
    fit::bridge<void, Error> child2_bridge;
    return ExpectBind(root_device,
                      [=, child1_completer = std::move(child1_bridge.completer),
                       child2_completer = std::move(child2_bridge.completer)](
                          HookInvocation record, Completer<void> completer) mutable {
                        std::vector<zx_device_prop_t> child1_props({
                            {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TEST},
                            {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_LIBDRIVER_TEST},
                            {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TEST_CHILD_1},
                        });
                        std::vector<zx_device_prop_t> child2_props({
                            {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TEST},
                            {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_LIBDRIVER_TEST},
                            {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TEST_CHILD_2},
                        });
                        ActionList actions;
                        actions.AppendAddMockDevice(loop_.dispatcher(), (*root_device)->path(),
                                                    "component1", std::move(child1_props), ZX_OK,
                                                    std::move(child1_completer), child1_device);
                        actions.AppendAddMockDevice(loop_.dispatcher(), (*root_device)->path(),
                                                    "component2", std::move(child2_props), ZX_OK,
                                                    std::move(child2_completer), child2_device);
                        actions.AppendReturnStatus(ZX_OK);

                        (*child1_device)->set_hooks(std::make_unique<IgnoreGetProtocol>());
                        (*child2_device)->set_hooks(std::make_unique<IgnoreGetProtocol>());
                        completer.complete_ok();
                        return actions;
                      })
        .and_then(child1_bridge.consumer.promise_or(::fit::error("child1 create abandoned")))
        .and_then(child2_bridge.consumer.promise_or(::fit::error("child2 create abandoned")));
  }
};

// This test creates two devices that match the well-known composite in the
// test sysdev driver.  It then waits for it to appear in devfs.
TEST_F(CompositeDeviceTest, CreateTest) {
  std::unique_ptr<RootMockDevice> root_device;
  std::unique_ptr<MockDevice> child_device1, child_device2;
  fidl::InterfacePtr<fuchsia::io::Node> client;

  auto promise = CreateComponentDevices(&root_device, &child_device1, &child_device2)
                     .and_then(DoWaitForPath("composite"))
                     .and_then([&]() -> Promise<void> { return DoOpen("composite", &client); })
                     .and_then([&]() -> Promise<void> {
                       // Destroy the test device.  This should cause an unbind of the child
                       // device.
                       root_device.reset();
                       return JoinPromises(ExpectUnbindThenRelease(child_device1),
                                           ExpectUnbindThenRelease(child_device2));
                     });

  RunPromise(std::move(promise));
}

// TODO(FLK-344): Re-enable once flake is fixed.
//
// This test creates the well-known composite, and force binds a test driver
// stack to the composite.  It then forces one of the components to unbind.
// It verifies that the composite mock-device's unbind hook is called.
TEST_F(CompositeDeviceTest, DISABLED_UnbindComponent) {
  std::unique_ptr<RootMockDevice> root_device, composite_mock;
  std::unique_ptr<MockDevice> child_device1, child_device2, composite_child_device;
  fidl::InterfacePtr<fuchsia::io::Node> client;
  fidl::InterfacePtr<fuchsia::device::Controller> composite, child1_controller;
  fidl::SynchronousInterfacePtr<fuchsia::device::test::RootDevice> composite_test;

  auto promise =
      CreateComponentDevices(&root_device, &child_device1, &child_device2)
          .and_then(DoWaitForPath("composite"))
          .and_then([&]() -> Promise<void> { return DoWaitForPath("composite/test"); })
          .and_then([&]() -> Promise<void> { return DoOpen("composite/test", &client); })
          .and_then([&]() -> Promise<void> {
            composite_test.Bind(client.Unbind().TakeChannel());

            auto bind_callback = [this, &composite_mock, &composite_child_device](
                                     HookInvocation record, Completer<void> completer) {
              // Create a test child that we can monitor for hooks.
              ActionList actions;
              actions.AppendAddMockDevice(loop_.dispatcher(), composite_mock->path(), "child",
                                          std::vector<zx_device_prop_t>{}, ZX_OK,
                                          std::move(completer), &composite_child_device);
              actions.AppendReturnStatus(ZX_OK);
              return actions;
            };

            fit::bridge<void, Error> bridge;
            auto bind_hook =
                std::make_unique<BindOnce>(std::move(bridge.completer), std::move(bind_callback));
            // Bind the mock device driver to a new child
            zx_status_t status = RootMockDevice::CreateFromTestRoot(
                devmgr_, loop_.dispatcher(), std::move(composite_test), std::move(bind_hook),
                &composite_mock);
            PROMISE_ASSERT(ASSERT_EQ(status, ZX_OK));

            return bridge.consumer.promise_or(::fit::error("bind abandoned"));
          })
          .and_then([&]() -> Promise<void> {
            // Open up child1, so we can send it an unbind request
            auto wait_for_open = DoOpen(child_device1->path(), &client);
            auto expect_open = ExpectOpen(child_device1, [](HookInvocation record, uint32_t flags,
                                                            Completer<void> completer) {
              completer.complete_ok();
              ActionList actions;
              actions.AppendReturnStatus(ZX_OK);
              return actions;
            });
            return expect_open.and_then(std::move(wait_for_open));
          })
          .and_then([&]() -> Promise<void> {
            // Send the unbind request to child1
            zx_status_t status =
                child1_controller.Bind(client.Unbind().TakeChannel(), loop_.dispatcher());
            PROMISE_ASSERT(ASSERT_EQ(status, ZX_OK));

            fit::bridge<void, Error> bridge;
            child1_controller->ScheduleUnbind(
                [completer = std::move(bridge.completer)](zx_status_t status) mutable {
                  if (status == ZX_OK) {
                    completer.complete_ok();
                  } else {
                    completer.complete_error(std::string("unbind failed"));
                  }
                });

            // We should receive the unbind for child1, and then soon after for
            // the composite.
            auto unbind_promise =
                ExpectUnbind(child_device1, [](HookInvocation record, Completer<void> completer) {
                  ActionList actions;
                  // We don't care about when the unbind reply actually finishes.
                  // The ExpectRelease below will serialize against it anyway.
                  Promise<void> unbind_reply_done;
                  actions.AppendUnbindReply(&unbind_reply_done);
                  // Complete here instead of in remove device, since the remove
                  // device completion doesn't fire until after we're notified,
                  // which might be after the unbind of the composite begins
                  completer.complete_ok();
                  return actions;
                }).and_then(ExpectUnbindThenRelease(composite_child_device));

            return unbind_promise.and_then(
                bridge.consumer.promise_or(::fit::error("Unbind abandoned")));
          })
          .and_then([&]() -> Promise<void> {
            child1_controller.Unbind();
            return ExpectClose(child_device1, [](HookInvocation record, uint32_t flags,
                                                 Completer<void> completer) {
              completer.complete_ok();
              ActionList actions;
              actions.AppendReturnStatus(ZX_OK);
              return actions;
            });
          })
          .and_then(ExpectRelease(child_device1))
          .and_then([&]() -> Promise<void> {
            // Destroy the test device.  This should cause an unbind of the last child
            // device.
            root_device.reset();
            return ExpectUnbindThenRelease(child_device2);
          });

  RunPromise(std::move(promise));
}

}  // namespace libdriver_integration_test
