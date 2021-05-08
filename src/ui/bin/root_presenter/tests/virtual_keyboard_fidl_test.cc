// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/test_loop_fixture.h>

#include "src/ui/bin/root_presenter/virtual_keyboard_controller.h"
#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"

namespace root_presenter {
namespace virtual_keyboard_fidl {
namespace {

// Tests the virtual keyboard subsystem through the FIDL interfaces exposed
// by the objects that compose the subsystem.
class VirtualKeyboardFidlTest : public gtest::TestLoopFixture {
 protected:
  VirtualKeyboardFidlTest() : coordinator_(context_provider_.context()) {}

  template <typename Interface>
  void ConnectToPublicService(fidl::InterfaceRequest<Interface> request) {
    context_provider_.ConnectToPublicService(std::move(request));
  }

  auto CreateManagerClient() {
    fuchsia::input::virtualkeyboard::ManagerPtr client;
    ConnectToPublicService(client.NewRequest());
    return client;
  }

  std::tuple<fuchsia::input::virtualkeyboard::ControllerPtr, fuchsia::ui::views::ViewRefControl>
  CreateControllerClient() {
    // Connect to `ControllerCreator` protocol.
    fuchsia::input::virtualkeyboard::ControllerCreatorPtr controller_creator;
    ConnectToPublicService(controller_creator.NewRequest());

    // Create a `Controller`.
    fuchsia::input::virtualkeyboard::ControllerPtr controller;
    auto view_ref_pair = scenic::ViewRefPair::New();
    controller_creator->Create(std::move(view_ref_pair.view_ref),
                               fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC,
                               controller.NewRequest());

    return {std::move(controller), std::move(view_ref_pair.control_ref)};
  }

 private:
  sys::testing::ComponentContextProvider context_provider_;
  VirtualKeyboardCoordinator coordinator_;
};

// Tests which verify that the virtual keyboard subsystem registers the `Discoverable`
// protocols in the `fuchsia.input.virtualkeyboard` library.
namespace protocol_registration {
TEST_F(VirtualKeyboardFidlTest, RegistersControllerCreatorService) {
  zx_status_t status = ZX_OK;
  fuchsia::input::virtualkeyboard::ControllerCreatorPtr controller_creator;
  ConnectToPublicService(controller_creator.NewRequest());
  controller_creator.set_error_handler([&status](zx_status_t stat) { status = stat; });

  fuchsia::input::virtualkeyboard::ControllerPtr controller;
  auto view_ref_pair = scenic::ViewRefPair::New();
  controller_creator->Create(std::move(view_ref_pair.view_ref),
                             fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC,
                             controller.NewRequest());
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_OK, status) << "status = " << zx_status_get_string(status);
}

TEST_F(VirtualKeyboardFidlTest, RegistersManagerService) {
  zx_status_t status = ZX_OK;
  auto manager = CreateManagerClient();
  ConnectToPublicService(manager.NewRequest());
  manager.set_error_handler([&status](zx_status_t stat) { status = stat; });
  manager->Notify(true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
                  []() {});
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_OK, status) << "status = " << zx_status_get_string(status);
}
}  // namespace protocol_registration

// Tests which validate how connections to `fuchsia.input.virtualkeyboard.Controller` are handled.
namespace fuchsia_input_virtualkeyboard_controller_connections {
TEST_F(VirtualKeyboardFidlTest, ClosingCreatorDoesNotCloseController) {
  // Note: this test creates the controller manually (instead of using CreateControllerClient()),
  // because this test
  // a) wants to set an error handler on the ControllerCreator
  // b) wants to be explicit about the lifetime of the ControllerCreator

  // Connect to `ControllerCreator` protocol.
  fuchsia::input::virtualkeyboard::ControllerCreatorPtr controller_creator;
  ConnectToPublicService(controller_creator.NewRequest());

  // Create controller.
  zx_status_t controller_status = ZX_OK;
  fuchsia::input::virtualkeyboard::ControllerPtr controller;
  auto view_ref_pair1 = scenic::ViewRefPair::New();
  controller_creator->Create(std::move(view_ref_pair1.view_ref),
                             fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC,
                             controller.NewRequest());
  controller.set_error_handler(
      [&controller_status](zx_status_t stat) { controller_status = stat; });
  RunLoopUntilIdle();

  // Close the `ControllerCreator` connection.
  controller_creator.Unbind();
  RunLoopUntilIdle();

  // Call a method on the `Controller`, and verify no error occurred.
  controller->RequestShow();
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_OK, controller_status) << "status = " << zx_status_get_string(controller_status);
}

TEST_F(VirtualKeyboardFidlTest, LastControllerHasPriority) {
  // Create first controller.
  zx_status_t controller1_status = ZX_OK;
  auto [controller1, view_ref_control1] = CreateControllerClient();
  controller1.set_error_handler(
      [&controller1_status](zx_status_t stat) { controller1_status = stat; });
  RunLoopUntilIdle();

  // Create second controller.
  zx_status_t controller2_status = ZX_OK;
  auto [controller2, view_ref_control2] = CreateControllerClient();
  controller2.set_error_handler(
      [&controller2_status](zx_status_t stat) { controller2_status = stat; });
  RunLoopUntilIdle();

  // Both clients try to call `RequestShow()`.
  controller1->RequestShow();
  controller2->RequestShow();

  // The request to the first controller should fail, since we only support a single
  // controller at a time, and the second controller replaces the first one.
  //
  // Note: we'll need to update this test when we add support for multiple
  // simultaneous controllers.
  ASSERT_NE(ZX_OK, controller1_status) << "status = " << zx_status_get_string(controller1_status);
  ASSERT_EQ(ZX_OK, controller2_status) << "status = " << zx_status_get_string(controller2_status);
}
}  // namespace fuchsia_input_virtualkeyboard_controller_connections

// Tests which validate how connections to `fuchsia.input.virtualkeyboard.Manager` are handled.
namespace fuchsia_input_virtualkeyboard_manager_connections {
TEST_F(VirtualKeyboardFidlTest, FirstManagerClientHasPriority) {
  // First client tries to connect.
  zx_status_t client1_status = ZX_OK;
  auto client1 = CreateManagerClient();
  client1.set_error_handler([&client1_status](zx_status_t stat) { client1_status = stat; });
  RunLoopUntilIdle();

  // Second client tries to connect.
  zx_status_t client2_status = ZX_OK;
  auto client2 = CreateManagerClient();
  client2.set_error_handler([&client2_status](zx_status_t stat) { client2_status = stat; });
  RunLoopUntilIdle();

  // Both clients try to call `Notify()`.
  client1->Notify(true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
                  []() {});
  client2->Notify(true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
                  []() {});

  ASSERT_EQ(ZX_OK, client1_status) << "status = " << zx_status_get_string(client1_status);
  ASSERT_NE(ZX_OK, client2_status) << "status = " << zx_status_get_string(client2_status);
}

TEST_F(VirtualKeyboardFidlTest, NewManagerClientCanConnectAfterFirstDisconnects) {
  {
    // First client connects and calls Notify().
    zx_status_t status = ZX_OK;
    auto client = CreateManagerClient();
    client.set_error_handler([&status](zx_status_t stat) { status = stat; });
    client->Notify(true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
                   []() {});
    RunLoopUntilIdle();
    ASSERT_EQ(ZX_OK, status) << "status = " << zx_status_get_string(status);
  }

  // Run event loop, to process side-effects of `client` going away.
  RunLoopUntilIdle();

  {
    // Second client connects and calls Notify().
    zx_status_t status = ZX_OK;
    auto client = CreateManagerClient();
    client.set_error_handler([&status](zx_status_t stat) { status = stat; });
    client->Notify(true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
                   []() {});
    RunLoopUntilIdle();
    ASSERT_EQ(ZX_OK, status) << "status = " << zx_status_get_string(status);
  }
}
}  // namespace fuchsia_input_virtualkeyboard_manager_connections

// Tests that verify the behavior of the methods of `fuchsia.input.virtualkeyboard.Manager`.
namespace fuchsia_input_virtualkeyboard_manager_methods {
TEST_F(VirtualKeyboardFidlTest, WatchTypeAndVisibilityDoesNotError) {
  zx_status_t status = ZX_OK;
  auto manager = CreateManagerClient();
  manager.set_error_handler([&status](zx_status_t stat) { status = stat; });
  manager->WatchTypeAndVisibility(
      [](fuchsia::input::virtualkeyboard::TextType reason, bool is_visible) {});
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_OK, status) << "status = " << zx_status_get_string(status);
}

TEST_F(VirtualKeyboardFidlTest, NotifyDoesNotError) {
  zx_status_t status = ZX_OK;
  auto manager = CreateManagerClient();
  manager.set_error_handler([&status](zx_status_t stat) { status = stat; });
  manager->Notify(true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
                  []() {});
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_OK, status) << "status = " << zx_status_get_string(status);
}
}  // namespace fuchsia_input_virtualkeyboard_manager_methods

}  // namespace
}  // namespace virtual_keyboard_fidl
}  // namespace root_presenter
