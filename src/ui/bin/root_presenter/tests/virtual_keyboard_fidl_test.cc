// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <zircon/errors.h>
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

  std::tuple<fuchsia::input::virtualkeyboard::ControllerPtr, fuchsia::ui::views::ViewRef,
             fuchsia::ui::views::ViewRefControl>
  CreateControllerClient(fuchsia::input::virtualkeyboard::TextType initial_text_type =
                             fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC) {
    // Connect to `ControllerCreator` protocol.
    fuchsia::input::virtualkeyboard::ControllerCreatorPtr controller_creator;
    ConnectToPublicService(controller_creator.NewRequest());

    // Create a `Controller`.
    fuchsia::input::virtualkeyboard::ControllerPtr controller;
    auto view_ref_pair = scenic::ViewRefPair::New();
    controller_creator->Create(fidl::Clone(view_ref_pair.view_ref), initial_text_type,
                               controller.NewRequest());

    return {std::move(controller), std::move(view_ref_pair.view_ref),
            std::move(view_ref_pair.control_ref)};
  }

 private:
  sys::testing::ComponentContextProvider context_provider_;
  FidlBoundVirtualKeyboardCoordinator coordinator_;
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

TEST_F(VirtualKeyboardFidlTest, MultipleControllersAreSupported) {
  // Create first controller.
  zx_status_t controller1_status = ZX_OK;
  auto [controller1, view_ref1, view_ref_control1] = CreateControllerClient();
  controller1.set_error_handler(
      [&controller1_status](zx_status_t stat) { controller1_status = stat; });
  RunLoopUntilIdle();

  // Create second controller.
  zx_status_t controller2_status = ZX_OK;
  auto [controller2, view_ref2, view_ref_control2] = CreateControllerClient();
  controller2.set_error_handler(
      [&controller2_status](zx_status_t stat) { controller2_status = stat; });
  RunLoopUntilIdle();

  // Verify that te first controller can invoke a method.
  controller1->RequestShow();
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_OK, controller1_status) << "status = " << zx_status_get_string(controller1_status);

  // Verify that the second controller can invoke a method.
  controller2->RequestHide();
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_OK, controller2_status) << "status = " << zx_status_get_string(controller2_status);
}
}  // namespace fuchsia_input_virtualkeyboard_controller_connections

// Tests that verify the behavior of the methods of `fuchsia.input.virtualkeyboard.Controller`.
//
// Note: these tests focus on the values/errors returned by Controller methods, _not_ how these
// methods affect values returned to calls on other protocols.
//
// To see, for example, how Controller.RequestShow() resolves a hanging get call to
// Manager.WatchtypeAndVisibility(), see the fuchsia_input_virtualkeyboard_manager_methods
// tests.
namespace fuchsia_input_virtualkeyboard_controller_methods {

TEST_F(VirtualKeyboardFidlTest, SetTextTypeDoesNotError) {
  // Create controller.
  zx_status_t controller_status = ZX_OK;
  auto [controller, view_ref, view_ref_control] = CreateControllerClient();
  controller.set_error_handler(
      [&controller_status](zx_status_t stat) { controller_status = stat; });

  // Invoke SetTextType(), and verify there is no error on the channel.
  controller->SetTextType(fuchsia::input::virtualkeyboard::TextType::PHONE);
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_OK, controller_status) << "status = " << zx_status_get_string(controller_status);
}

TEST_F(VirtualKeyboardFidlTest, RequestShowDoesNotError) {
  // Create controller.
  zx_status_t controller_status = ZX_OK;
  auto [controller, view_ref, view_ref_control] = CreateControllerClient();
  controller.set_error_handler(
      [&controller_status](zx_status_t stat) { controller_status = stat; });

  // Invoke RequestShow(), and verify there is no error on the channel.
  controller->RequestShow();
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_OK, controller_status) << "status = " << zx_status_get_string(controller_status);
}

TEST_F(VirtualKeyboardFidlTest, RequestHideDoesNotError) {
  // Create controller.
  zx_status_t controller_status = ZX_OK;
  auto [controller, view_ref, view_ref_control] = CreateControllerClient();
  controller.set_error_handler(
      [&controller_status](zx_status_t stat) { controller_status = stat; });

  // Invoke RequestHide(), and verify there is no error on the channel.
  controller->RequestHide();
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_OK, controller_status) << "status = " << zx_status_get_string(controller_status);
}

TEST_F(VirtualKeyboardFidlTest, WatchVisibility_FirstCallReturnsImmediately) {
  // Create controller.
  auto [controller, view_ref, view_ref_control] = CreateControllerClient();

  // Send watch.
  bool got_watch_visibility_result = false;
  controller->WatchVisibility(
      [&got_watch_visibility_result](bool vis) { got_watch_visibility_result = true; });
  RunLoopUntilIdle();

  // Verify watch completed immediately.
  ASSERT_TRUE(got_watch_visibility_result);
}

TEST_F(VirtualKeyboardFidlTest, WatchVisibility_SecondCallHangs) {
  // Create controller.
  zx_status_t controller_status = ZX_OK;
  auto [controller, view_ref, view_ref_control] = CreateControllerClient();
  controller.set_error_handler(
      [&controller_status](zx_status_t stat) { controller_status = stat; });

  // Send first watch, which completes immediately.
  controller->WatchVisibility([](bool vis) {});
  RunLoopUntilIdle();

  // Send second watch, which hangs.
  bool got_watch_visibility_result = false;
  controller->WatchVisibility(
      [&got_watch_visibility_result](bool vis) { got_watch_visibility_result = true; });
  RunLoopUntilIdle();
  ASSERT_FALSE(got_watch_visibility_result);
  ASSERT_EQ(ZX_OK, controller_status) << "status = " << zx_status_get_string(controller_status);
}

TEST_F(VirtualKeyboardFidlTest, WatchVisibility_SecondCallIsResolvedByOwnRequestShow) {
  // Create controller.
  auto [controller, view_ref, view_ref_control] = CreateControllerClient();

  // Send first watch, which completes immediately.
  controller->WatchVisibility([](bool vis) {});
  RunLoopUntilIdle();

  // Second second watch, and let it hang.
  bool got_watch_visibility_result = false;
  controller->WatchVisibility(
      [&got_watch_visibility_result](bool vis) { got_watch_visibility_result = true; });
  RunLoopUntilIdle();

  // Request the keyboard to be shown. This changes the state of the keyboard, since
  // the default state is hidden.
  controller->RequestShow();
  RunLoopUntilIdle();

  // Verify that the watch completed.
  //
  // Note: when we incorporate focus state into VirtualKeyboardCoordinator, we'll need
  // to update this test. (The watch should not complete until the `View` associated with
  // `view_ref_pair` has focus.)
  ASSERT_TRUE(got_watch_visibility_result);
}

TEST_F(VirtualKeyboardFidlTest, WatchVisibility_SecondCallIsNotResolvedByOwnRequestHide) {
  // Create controller.
  zx_status_t controller_status = ZX_OK;
  auto [controller, view_ref, view_ref_control] = CreateControllerClient();
  controller.set_error_handler(
      [&controller_status](zx_status_t stat) { controller_status = stat; });

  // Send first watch, which completes immediately.
  controller->WatchVisibility([](bool vis) {});
  RunLoopUntilIdle();

  // Second second watch, and let it hang.
  bool got_watch_visibility_result = false;
  controller->WatchVisibility(
      [&got_watch_visibility_result](bool vis) { got_watch_visibility_result = true; });
  RunLoopUntilIdle();

  // Request the keyboard to be hidden. This does _not_ change the state of the keyboard,
  // since the default state is also hidden.
  controller->RequestHide();
  RunLoopUntilIdle();

  // Verify that the watch did not complete.
  ASSERT_FALSE(got_watch_visibility_result);
  ASSERT_EQ(ZX_OK, controller_status) << "status = " << zx_status_get_string(controller_status);
}

TEST_F(VirtualKeyboardFidlTest,
       WatchVisibility_SecondCallIsResolvedByManagerReportOfUserInteraction) {
  // Create controller.
  auto [controller, view_ref, view_ref_control] = CreateControllerClient();

  // Send first watch, which completes immediately.
  controller->WatchVisibility([](bool vis) {});
  RunLoopUntilIdle();

  // Second second watch, and let it hang.
  bool got_watch_visibility_result = false;
  controller->WatchVisibility(
      [&got_watch_visibility_result](bool vis) { got_watch_visibility_result = true; });
  RunLoopUntilIdle();

  // Create Manager, and call Notify().
  auto manager = CreateManagerClient();
  manager->Notify(true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
                  []() {});
  RunLoopUntilIdle();

  // Verify that the watch completed.
  ASSERT_TRUE(got_watch_visibility_result);
}

TEST_F(VirtualKeyboardFidlTest, WatchVisibility_AllControllersAreToldOfUserInteraction) {
  // Create controller.
  auto [controller1, view_ref1, view_ref_control1] = CreateControllerClient();
  auto [controller2, view_ref2, view_ref_control2] = CreateControllerClient();

  // Send first watch for each controller, which completes immediately.
  controller1->WatchVisibility([](bool vis) {});
  controller2->WatchVisibility([](bool vis) {});
  RunLoopUntilIdle();

  // Second second watch on each controller, and let them hang.
  bool c1_got_watch_visibility_result = false;
  bool c2_got_watch_visibility_result = false;
  controller1->WatchVisibility([&](bool vis) { c1_got_watch_visibility_result = true; });
  controller2->WatchVisibility([&](bool vis) { c2_got_watch_visibility_result = true; });
  RunLoopUntilIdle();

  // Create Manager, and call Notify().
  auto manager = CreateManagerClient();
  manager->Notify(true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
                  []() {});
  RunLoopUntilIdle();

  // Verify that the watch completed.
  ASSERT_TRUE(c1_got_watch_visibility_result);
  ASSERT_TRUE(c2_got_watch_visibility_result);
}

}  // namespace fuchsia_input_virtualkeyboard_controller_methods

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

TEST_F(VirtualKeyboardFidlTest, NewManagerClientCanConnectAndNotifyAfterFirstDisconnects) {
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

TEST_F(VirtualKeyboardFidlTest, NewManagerClientCanConnectAndWatchAfterFirstDisconnects) {
  {
    // Create first Manager client, and have the client call WatchTypeAndVisibility().
    zx_status_t status = ZX_OK;
    auto client = CreateManagerClient();
    bool did_watch_complete = false;
    client.set_error_handler([&status](zx_status_t stat) { status = stat; });
    client->WatchTypeAndVisibility([&did_watch_complete](fuchsia::input::virtualkeyboard::TextType,
                                                         bool) { did_watch_complete = true; });

    // Manager client connection should be ok, and the client's WatchTypeAndVisibility() call
    // should have returned.
    RunLoopUntilIdle();
    ASSERT_EQ(ZX_OK, status) << "status = " << zx_status_get_string(status);
    ASSERT_TRUE(did_watch_complete);
  }

  // Run event loop, to process side-effects of `client` going away.
  RunLoopUntilIdle();

  {
    // Create second Manager client, and have the client call WatchTypeAndVisibility().
    zx_status_t status = ZX_OK;
    auto client = CreateManagerClient();
    bool did_watch_complete = false;
    client.set_error_handler([&status](zx_status_t stat) { status = stat; });
    client->WatchTypeAndVisibility([&did_watch_complete](fuchsia::input::virtualkeyboard::TextType,
                                                         bool) { did_watch_complete = true; });

    // Manager client connection should be ok, and the client's WatchTypeAndVisibility() call
    // should have returned.
    RunLoopUntilIdle();
    ASSERT_EQ(ZX_OK, status) << "status = " << zx_status_get_string(status);
    ASSERT_TRUE(did_watch_complete);
  }
}

TEST_F(VirtualKeyboardFidlTest, ManagerDisconnectsOnConcurrentWatches) {
  // Connect client.
  zx_status_t status = ZX_OK;
  auto client = CreateManagerClient();
  client.set_error_handler([&status](zx_status_t stat) { status = stat; });

  // Send first watch, which completes immediately.
  client->WatchTypeAndVisibility([](fuchsia::input::virtualkeyboard::TextType, bool) {});
  RunLoopUntilIdle();

  // Now, set up two concurrent watches.
  client->WatchTypeAndVisibility([](fuchsia::input::virtualkeyboard::TextType, bool) {});
  client->WatchTypeAndVisibility([](fuchsia::input::virtualkeyboard::TextType, bool) {});
  RunLoopUntilIdle();

  // Verify that the channel was closed, with the expected epitaph.
  ASSERT_EQ(ZX_ERR_BAD_STATE, status) << "status = " << zx_status_get_string(status);
}

TEST_F(VirtualKeyboardFidlTest, ClientDisconnectionNotifiesControllersThatKeyboardIsHidden) {
  // Create controller, and set visibility to true.
  auto [controller, view_ref, view_ref_control] = CreateControllerClient();
  controller->RequestShow();
  RunLoopUntilIdle();

  // Send a watch request, which will complete immediately.
  std::optional<bool> first_watcher_visibility;
  controller->WatchVisibility(
      [&first_watcher_visibility](bool vis) { first_watcher_visibility = vis; });
  RunLoopUntilIdle();
  ASSERT_EQ(true, first_watcher_visibility);

  // Create manager.
  std::optional<fuchsia::input::virtualkeyboard::ManagerPtr> manager = CreateManagerClient();

  // Set up a watch.
  std::optional<bool> second_visibility_result;
  controller->WatchVisibility(
      [&second_visibility_result](bool vis) { second_visibility_result = vis; });
  RunLoopUntilIdle();

  // Disconnect the maanger.
  manager.reset();
  RunLoopUntilIdle();

  // Verify that the controller learned that the keyboard was hidden.
  ASSERT_EQ(false, second_visibility_result);
}

TEST_F(VirtualKeyboardFidlTest, NewManagerClientCanConnectAfterFirstIsDisconnectedByError) {
  // Connect client, and set up concurrent watches.
  auto client1 = CreateManagerClient();

  // Send first watch, which completes immediately.
  client1->WatchTypeAndVisibility([](fuchsia::input::virtualkeyboard::TextType, bool) {});
  RunLoopUntilIdle();

  // Set up two concurrent watches, to force closure of client1.
  client1->WatchTypeAndVisibility([](fuchsia::input::virtualkeyboard::TextType, bool) {});
  client1->WatchTypeAndVisibility([](fuchsia::input::virtualkeyboard::TextType, bool) {});
  RunLoopUntilIdle();

  // Second client connects and calls Notify().
  zx_status_t status = ZX_OK;
  auto client2 = CreateManagerClient();
  client2.set_error_handler([&status](zx_status_t stat) { status = stat; });
  client2->Notify(true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
                  []() {});
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_OK, status) << "status = " << zx_status_get_string(status);
}
}  // namespace fuchsia_input_virtualkeyboard_manager_connections

// Tests that verify the behavior of the methods of `fuchsia.input.virtualkeyboard.Manager`.
//
// Note: these tests focus on the values/errors returned by Manager methods, _not_ how these
// methods affect values returned to calls on other protocols.
//
// To see, for example, how Manager.Notify() resolves a hanging get call to
// Controller.WatchVisibility(), see the fuchsia_input_virtualkeyboard_controller_methods
// tests.
namespace fuchsia_input_virtualkeyboard_manager_methods {

TEST_F(VirtualKeyboardFidlTest, WatchTypeAndVisibility_FirstCallReturnsImmediately) {
  auto manager = CreateManagerClient();
  bool was_called = false;
  manager->WatchTypeAndVisibility([&](fuchsia::input::virtualkeyboard::TextType reason,
                                      bool is_visible) { was_called = true; });
  RunLoopUntilIdle();
  ASSERT_TRUE(was_called);
}

TEST_F(VirtualKeyboardFidlTest, WatchTypeAndVisibility_SecondCallHangs) {
  // Create manager.
  zx_status_t manager_status = ZX_OK;
  auto manager = CreateManagerClient();
  manager.set_error_handler([&](zx_status_t stat) { manager_status = stat; });

  // Send first watch, which completes immediately.
  manager->WatchTypeAndVisibility(
      [](fuchsia::input::virtualkeyboard::TextType reason, bool is_visible) {});

  // Send second watch, which hangs.
  bool was_called = false;
  manager->WatchTypeAndVisibility([&](fuchsia::input::virtualkeyboard::TextType reason,
                                      bool is_visible) { was_called = true; });
  RunLoopUntilIdle();
  ASSERT_FALSE(was_called);
  ASSERT_EQ(ZX_OK, manager_status) << "status = " << zx_status_get_string(manager_status);
}

TEST_F(VirtualKeyboardFidlTest, WatchTypeAndVisibility_SecondCallIsResolvedByRequestShow) {
  // Create manager.
  auto manager = CreateManagerClient();

  // Send first watch, which completes immediately.
  manager->WatchTypeAndVisibility(
      [](fuchsia::input::virtualkeyboard::TextType reason, bool is_visible) {});

  // Send second watch, which hangs.
  bool was_called = false;
  manager->WatchTypeAndVisibility([&](fuchsia::input::virtualkeyboard::TextType reason,
                                      bool is_visible) { was_called = true; });
  RunLoopUntilIdle();

  // Create a Controller, and ask for the keyboard to be shown. This changes the state of
  // the keyboard, since the default state is hidden.
  auto [controller, view_ref, view_ref_control] = CreateControllerClient();
  controller->RequestShow();
  RunLoopUntilIdle();

  ASSERT_TRUE(was_called);
}

TEST_F(VirtualKeyboardFidlTest, WatchTypeAndVisibility_SecondCallIsNotResolvedByRequestHide) {
  // Create manager.
  zx_status_t manager_status = ZX_OK;
  auto manager = CreateManagerClient();
  manager.set_error_handler([&](zx_status_t stat) { manager_status = stat; });

  // Send first watch, which completes immediately.
  manager->WatchTypeAndVisibility(
      [](fuchsia::input::virtualkeyboard::TextType reason, bool is_visible) {});

  // Send second watch, which hangs.
  bool was_called = false;
  manager->WatchTypeAndVisibility([&](fuchsia::input::virtualkeyboard::TextType reason,
                                      bool is_visible) { was_called = true; });
  RunLoopUntilIdle();

  // Create a Controller, and ask for the keyboard to be hidden. This does _not_ change the
  // state of the keyboard, since the default state is also hidden.
  auto [controller, view_ref, view_ref_control] = CreateControllerClient();
  controller->RequestHide();
  RunLoopUntilIdle();

  ASSERT_FALSE(was_called);
  ASSERT_EQ(ZX_OK, manager_status) << "status = " << zx_status_get_string(manager_status);
}

TEST_F(VirtualKeyboardFidlTest, WatchTypeAndVisibility_SecondCallIsResolvedBySetTextType) {
  // Create manager.
  auto manager = CreateManagerClient();

  // Send first watch, which completes immediately.
  manager->WatchTypeAndVisibility(
      [](fuchsia::input::virtualkeyboard::TextType reason, bool is_visible) {});

  // Send second watch, which hangs.
  bool was_called = false;
  manager->WatchTypeAndVisibility([&](fuchsia::input::virtualkeyboard::TextType reason,
                                      bool is_visible) { was_called = true; });
  RunLoopUntilIdle();

  // Create a Controller, then change the text type.
  auto [controller, view_ref, view_ref_control] =
      CreateControllerClient(fuchsia::input::virtualkeyboard::TextType::NUMERIC);
  controller->SetTextType(fuchsia::input::virtualkeyboard::TextType::PHONE);
  RunLoopUntilIdle();

  ASSERT_TRUE(was_called);
}

TEST_F(VirtualKeyboardFidlTest, WatchTypeAndVisibility_ReceivesConfigSetBeforeManagerConnection) {
  // Create a Controller, and request that the keyboard be shown.
  auto [controller, view_ref, view_ref_control] =
      CreateControllerClient(fuchsia::input::virtualkeyboard::TextType::NUMERIC);
  controller->RequestShow();
  RunLoopUntilIdle();

  // Create manager.
  auto manager = CreateManagerClient();

  // Try to get the visibility of the keyboard.
  std::optional<bool> is_visible;
  manager->WatchTypeAndVisibility(
      [&](fuchsia::input::virtualkeyboard::TextType reason, bool is_vis) { is_visible = is_vis; });
  RunLoopUntilIdle();

  ASSERT_EQ(true, is_visible);
}

TEST_F(VirtualKeyboardFidlTest,
       WatchTypeAndVisibility_SecondNewManagerDoesNotReceiveBufferedConfig) {
  // Create a Controller, and request that the keyboard be shown.
  auto [controller, view_ref, view_ref_control] =
      CreateControllerClient(fuchsia::input::virtualkeyboard::TextType::NUMERIC);
  controller->RequestShow();
  RunLoopUntilIdle();

  {
    // Create first manager.
    auto manager = CreateManagerClient();

    // Get configuration.
    manager->WatchTypeAndVisibility(
        [](fuchsia::input::virtualkeyboard::TextType type, bool is_vis) {});
    RunLoopUntilIdle();
  }

  {
    // Create second manager.
    auto manager = CreateManagerClient();

    // Get configuration.
    std::optional<fuchsia::input::virtualkeyboard::TextType> text_type;
    manager->WatchTypeAndVisibility(
        [&](fuchsia::input::virtualkeyboard::TextType type, bool is_vis) { text_type = type; });
    RunLoopUntilIdle();
    ASSERT_NE(fuchsia::input::virtualkeyboard::TextType::NUMERIC, text_type);
  }
}

TEST_F(VirtualKeyboardFidlTest,
       WatchTypeAndVisibility_GetsCorrectVisibilityAfterRaceOnProgrammaticChangeNotification) {
  // Create controller and manager.
  auto [controller, view_ref, view_ref_control] =
      CreateControllerClient(fuchsia::input::virtualkeyboard::TextType::NUMERIC);
  auto manager = CreateManagerClient();

  // Request the keyboard to be hidden.
  controller->RequestHide();
  RunLoopUntilIdle();

  // Request the keyboard to be shown.
  controller->RequestShow();
  RunLoopUntilIdle();

  // Echo back the first request. We deliberately send this _after_ the RequestShow() above.
  manager->Notify(false, fuchsia::input::virtualkeyboard::VisibilityChangeReason::PROGRAMMATIC,
                  []() {});
  RunLoopUntilIdle();

  // Modify the text type.
  controller->SetTextType(::fuchsia::input::virtualkeyboard::TextType::PHONE);
  RunLoopUntilIdle();

  // Verify that the keyboard is still shown.
  std::optional<bool> actual_visibility;
  manager->WatchTypeAndVisibility([&](fuchsia::input::virtualkeyboard::TextType reason,
                                      bool is_visible) { actual_visibility = is_visible; });
  RunLoopUntilIdle();
  ASSERT_EQ(true, actual_visibility);
}

TEST_F(VirtualKeyboardFidlTest, NotifyIsAcked) {
  bool got_ack = false;
  zx_status_t status = ZX_OK;
  auto manager = CreateManagerClient();
  manager.set_error_handler([&status](zx_status_t stat) { status = stat; });
  manager->Notify(true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
                  [&got_ack]() { got_ack = true; });
  RunLoopUntilIdle();
  ASSERT_EQ(ZX_OK, status) << "status = " << zx_status_get_string(status);
  ASSERT_EQ(true, got_ack);
}
}  // namespace fuchsia_input_virtualkeyboard_manager_methods

}  // namespace
}  // namespace virtual_keyboard_fidl
}  // namespace root_presenter
