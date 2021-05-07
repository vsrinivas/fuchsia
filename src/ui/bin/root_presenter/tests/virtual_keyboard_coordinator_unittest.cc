// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/test_loop_fixture.h>

namespace root_presenter {
namespace virtual_keyboard_coordinator {
namespace {

class VirtualKeyboardCoordinatorTest : public gtest::TestLoopFixture {
 protected:
  auto* context_provider() { return &context_provider_; }

 private:
  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(VirtualKeyboardCoordinatorTest, CtorRegistersControllerCreatorService) {
  VirtualKeyboardCoordinator coordinator(context_provider()->context());

  zx_status_t status = ZX_OK;
  fuchsia::input::virtualkeyboard::ControllerCreatorPtr controller_creator_proxy;
  context_provider()->ConnectToPublicService(controller_creator_proxy.NewRequest());
  controller_creator_proxy.set_error_handler([&status](zx_status_t stat) { status = stat; });

  fuchsia::input::virtualkeyboard::ControllerPtr controller_proxy;
  auto view_ref_pair = scenic::ViewRefPair::New();
  controller_creator_proxy->Create(std::move(view_ref_pair.view_ref),
                                   fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC,
                                   controller_proxy.NewRequest());
  RunLoopUntilIdle();

  ASSERT_EQ(ZX_OK, status) << "status = " << zx_status_get_string(status);
}

}  // namespace
}  // namespace virtual_keyboard_coordinator
}  // namespace root_presenter
