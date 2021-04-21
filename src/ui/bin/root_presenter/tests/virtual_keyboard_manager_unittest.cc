// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_manager.h"

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <zircon/status.h>

#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/test_loop_fixture.h>

namespace root_presenter {
namespace virtual_keyboard_manager {
namespace {

class VirtualKeyboardManagerTest : public gtest::TestLoopFixture {
 protected:
  auto* context_provider() { return &context_provider_; }

 private:
  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(VirtualKeyboardManagerTest, CtorRegistersPublicService) {
  VirtualKeyboardManager manager(context_provider()->context());
  fuchsia::input::virtualkeyboard::ManagerPtr manager_proxy;
  zx_status_t status = ZX_OK;

  context_provider()->ConnectToPublicService(manager_proxy.NewRequest());
  manager_proxy.set_error_handler([&status](zx_status_t stat) { status = stat; });
  manager_proxy->Notify(
      true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION, []() {});
  RunLoopUntilIdle();

  ASSERT_EQ(ZX_OK, status) << "status = " << zx_status_get_string(status);
}

TEST_F(VirtualKeyboardManagerTest, FirstClientHasPriority) {
  VirtualKeyboardManager manager(context_provider()->context());

  // First client tries to connect.
  fuchsia::input::virtualkeyboard::ManagerPtr client1;
  zx_status_t client1_status = ZX_OK;
  context_provider()->ConnectToPublicService(client1.NewRequest());
  client1.set_error_handler([&client1_status](zx_status_t stat) { client1_status = stat; });
  RunLoopUntilIdle();

  // Second client tries to connect.
  fuchsia::input::virtualkeyboard::ManagerPtr client2;
  zx_status_t client2_status = ZX_OK;
  context_provider()->ConnectToPublicService(client2.NewRequest());
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

TEST_F(VirtualKeyboardManagerTest, NewClientCanConnectAfterFirstDisconnects) {
  VirtualKeyboardManager manager(context_provider()->context());

  {
    // First client connects and calls Notify().
    fuchsia::input::virtualkeyboard::ManagerPtr client;
    zx_status_t status = ZX_OK;
    context_provider()->ConnectToPublicService(client.NewRequest());
    client.set_error_handler([&status](zx_status_t stat) { status = stat; });
    client->Notify(true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
                   []() {});
    RunLoopUntilIdle();
    ASSERT_EQ(ZX_OK, status) << "status = " << zx_status_get_string(status);
  }

  // Run event loop, to process side-effects of client1 going away.
  RunLoopUntilIdle();

  {
    // First client connects and calls Notify().
    fuchsia::input::virtualkeyboard::ManagerPtr client;
    zx_status_t status = ZX_OK;
    context_provider()->ConnectToPublicService(client.NewRequest());
    client.set_error_handler([&status](zx_status_t stat) { status = stat; });
    client->Notify(true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
                   []() {});
    RunLoopUntilIdle();
    ASSERT_EQ(ZX_OK, status) << "status = " << zx_status_get_string(status);
  }
}

TEST_F(VirtualKeyboardManagerTest, WatchTypeAndVisibilityDoesNotCrash) {
  VirtualKeyboardManager(context_provider()->context())
      .WatchTypeAndVisibility(
          [](fuchsia::input::virtualkeyboard::TextType text_type, bool is_visible) {});
}

TEST_F(VirtualKeyboardManagerTest, NotifyDoesNotCrash) {
  VirtualKeyboardManager(context_provider()->context())
      .Notify(true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
              []() {});
}

}  // namespace
}  // namespace virtual_keyboard_manager
}  // namespace root_presenter
