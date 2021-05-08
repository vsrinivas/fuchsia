// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/test_loop_fixture.h>

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

}  // namespace
}  // namespace virtual_keyboard_fidl
}  // namespace root_presenter
