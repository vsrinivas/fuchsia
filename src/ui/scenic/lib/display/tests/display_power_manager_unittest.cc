// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/display_power_manager.h"

#include <fuchsia/ui/display/internal/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/async/time.h>

#include <thread>
#include <unordered_set>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/scenic/lib/display/display_manager.h"
#include "src/ui/scenic/lib/display/tests/mock_display_controller.h"

namespace scenic_impl {
namespace gfx {
namespace test {

namespace {

struct ChannelPair {
  zx::channel server;
  zx::channel client;
};

ChannelPair CreateChannelPair() {
  ChannelPair c;
  FX_CHECK(ZX_OK == zx::channel::create(0, &c.server, &c.client));
  return c;
}

}  // namespace

class DisplayPowerManagerMockTest : public gtest::RealLoopFixture {
 public:
  DisplayPowerManagerMockTest() {
    display_manager_ = std::make_unique<display::DisplayManager>([] {});
    display_power_manager_ = std::make_unique<display::DisplayPowerManager>(display_manager_.get());
  }

  display::DisplayManager* display_manager() { return display_manager_.get(); }
  display::DisplayPowerManager* display_power_manager() { return display_power_manager_.get(); }
  display::Display* display() { return display_manager()->default_display(); }

 private:
  std::unique_ptr<display::DisplayManager> display_manager_;
  std::unique_ptr<display::DisplayPowerManager> display_power_manager_;
};

TEST_F(DisplayPowerManagerMockTest, Ok) {
  const uint64_t kDisplayId = 0;
  const uint32_t kDisplayWidth = 1024;
  const uint32_t kDisplayHeight = 768;

  auto controller_channel = CreateChannelPair();

  display_manager()->BindDefaultDisplayController(
      fidl::InterfaceHandle<fuchsia::hardware::display::Controller>(
          std::move(controller_channel.client)));

  display_manager()->SetDefaultDisplayForTests(
      std::make_shared<display::Display>(kDisplayId, kDisplayWidth, kDisplayHeight));

  display::test::MockDisplayController mock_display_controller;
  mock_display_controller.Bind(std::move(controller_channel.server), dispatcher());
  mock_display_controller.set_set_display_power_result(ZX_OK);

  RunLoopUntilIdle();

  {
    bool callback_executed = false;
    std::thread set_display_power_thread([&callback_executed, this] {
      display_power_manager()->SetDisplayPower(
          /* power_on */ false,
          [&callback_executed](
              fuchsia::ui::display::internal::DisplayPower_SetDisplayPower_Result result) {
            callback_executed = true;
            EXPECT_TRUE(result.is_response());
          });
    });

    RunLoopUntil([&callback_executed] { return callback_executed; });
    set_display_power_thread.join();
    EXPECT_FALSE(mock_display_controller.display_power_on());
  }

  {
    bool callback_executed = false;
    std::thread set_display_power_thread([&callback_executed, this] {
      display_power_manager()->SetDisplayPower(
          /* power_on */ true,
          [&callback_executed](
              fuchsia::ui::display::internal::DisplayPower_SetDisplayPower_Result result) {
            callback_executed = true;
            EXPECT_TRUE(result.is_response());
          });
    });

    RunLoopUntil([&callback_executed] { return callback_executed; });
    set_display_power_thread.join();
    EXPECT_TRUE(mock_display_controller.display_power_on());
  }
}

TEST_F(DisplayPowerManagerMockTest, NoDisplay) {
  auto controller_channel = CreateChannelPair();

  display_manager()->BindDefaultDisplayController(
      fidl::InterfaceHandle<fuchsia::hardware::display::Controller>(
          std::move(controller_channel.client)));

  display_manager()->SetDefaultDisplayForTests(nullptr);

  display::test::MockDisplayController mock_display_controller;
  mock_display_controller.Bind(std::move(controller_channel.server), dispatcher());

  RunLoopUntilIdle();

  {
    bool callback_executed = false;
    std::thread set_display_power_thread([&callback_executed, this] {
      display_power_manager()->SetDisplayPower(
          /* power_on */ false,
          [&callback_executed](
              fuchsia::ui::display::internal::DisplayPower_SetDisplayPower_Result result) {
            callback_executed = true;
            ASSERT_TRUE(result.is_err());
            EXPECT_EQ(result.err(), ZX_ERR_NOT_FOUND);
          });
    });

    RunLoopUntil([&callback_executed] { return callback_executed; });
    set_display_power_thread.join();
  }
}

TEST_F(DisplayPowerManagerMockTest, NotSupported) {
  const uint64_t kDisplayId = 0;
  const uint32_t kDisplayWidth = 1024;
  const uint32_t kDisplayHeight = 768;

  auto controller_channel = CreateChannelPair();

  display_manager()->BindDefaultDisplayController(
      fidl::InterfaceHandle<fuchsia::hardware::display::Controller>(
          std::move(controller_channel.client)));

  display_manager()->SetDefaultDisplayForTests(
      std::make_shared<display::Display>(kDisplayId, kDisplayWidth, kDisplayHeight));

  display::test::MockDisplayController mock_display_controller;
  mock_display_controller.Bind(std::move(controller_channel.server), dispatcher());
  mock_display_controller.set_set_display_power_result(ZX_ERR_NOT_SUPPORTED);

  RunLoopUntilIdle();

  {
    bool callback_executed = false;
    std::thread set_display_power_thread([&callback_executed, this] {
      display_power_manager()->SetDisplayPower(
          /* power_on */ false,
          [&callback_executed](
              fuchsia::ui::display::internal::DisplayPower_SetDisplayPower_Result result) {
            callback_executed = true;
            EXPECT_TRUE(result.is_err());
            EXPECT_EQ(result.err(), ZX_ERR_NOT_SUPPORTED);
          });
    });

    RunLoopUntil([&callback_executed] { return callback_executed; });
    set_display_power_thread.join();
  }
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
