// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/displays/display_controller_listener.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/zx/channel.h>

#include <gtest/gtest.h>

#include "lib/fidl/cpp/comparison.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/gfx/tests/mock_display_controller.h"

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
  FXL_CHECK(ZX_OK == zx::channel::create(0, &c.server, &c.client));
  return c;
}

}  // namespace

class DisplayControllerListenerTest : public gtest::TestLoopFixture {
 public:
  void SetUp() {
    ChannelPair device_channel = CreateChannelPair();
    ChannelPair controller_channel = CreateChannelPair();

    mock_display_controller_driver_ = std::make_unique<MockDisplayController>();
    mock_display_controller_driver_->Bind(std::move(device_channel.server),
                                          std::move(controller_channel.server));

    zx_handle_t controller_handle = controller_channel.client.get();
    auto controller = std::make_shared<fuchsia::hardware::display::ControllerSyncPtr>();
    controller->Bind(std::move(controller_channel.client));
    display_controller_listener_ = std::make_unique<DisplayControllerListener>(
        std::move(device_channel.client), controller, controller_handle);
  }

  DisplayControllerListener* display_controller_listener() {
    return display_controller_listener_.get();
  };

  void ResetMockDisplayController() { mock_display_controller_driver_.reset(); }

  MockDisplayController* mock_display_controller_driver() {
    return mock_display_controller_driver_.get();
  }

 private:
  std::unique_ptr<MockDisplayController> mock_display_controller_driver_;
  std::unique_ptr<DisplayControllerListener> display_controller_listener_;
};

using DisplayControllerListenerBasicTest = gtest::TestLoopFixture;

// Verify the documented constructor behavior.
TEST_F(DisplayControllerListenerBasicTest, ConstructorArgs) {
  {
    // Valid arguments.
    ChannelPair device_channel = CreateChannelPair();
    ChannelPair controller_channel = CreateChannelPair();

    zx_handle_t controller_handle = controller_channel.client.get();
    auto controller = std::make_shared<fuchsia::hardware::display::ControllerSyncPtr>();
    controller->Bind(std::move(controller_channel.client));
    DisplayControllerListener listener(std::move(device_channel.client), controller,
                                       controller_handle);

    EXPECT_TRUE(listener.valid());
  }

  {
    zx::channel empty_channel;  // Invalid device.
    ChannelPair controller_channel = CreateChannelPair();

    zx_handle_t controller_handle = controller_channel.client.get();
    auto controller = std::make_shared<fuchsia::hardware::display::ControllerSyncPtr>();
    controller->Bind(std::move(controller_channel.client));
    DisplayControllerListener listener(std::move(empty_channel), controller, controller_handle);
    EXPECT_FALSE(listener.valid());
  }

  {
    ChannelPair device_channel = CreateChannelPair();
    ChannelPair controller_channel = CreateChannelPair();

    zx_handle_t controller_handle = controller_channel.client.get();
    // Unbound controller.
    auto controller = std::make_shared<fuchsia::hardware::display::ControllerSyncPtr>();
    DisplayControllerListener listener(std::move(device_channel.client), controller,
                                       controller_handle);

    EXPECT_FALSE(listener.valid());
  }

  {
    ChannelPair device_channel = CreateChannelPair();
    zx::channel empty_channel;  // Invalid controller.

    zx_handle_t controller_handle = empty_channel.get();
    auto controller = std::make_shared<fuchsia::hardware::display::ControllerSyncPtr>();
    controller->Bind(std::move(empty_channel));
    DisplayControllerListener listener(std::move(device_channel.client), controller,
                                       empty_channel.get());

    EXPECT_FALSE(listener.valid());
  }
}

// Verify that DisplayController connects to the FIDL service.
TEST_F(DisplayControllerListenerTest, Connect) {
  display_controller_listener()->InitializeCallbacks(/*on_invalid_cb=*/nullptr,
                                                     /*displays_changed_cb=*/nullptr,
                                                     /*client_ownership_change_cb=*/nullptr);

  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller_driver()->binding().is_bound());
  RunLoopUntilIdle();
  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller_driver()->binding().is_bound());
}

// Verify that DisplayController becomes invalid when the device channel is closed.
TEST_F(DisplayControllerListenerTest, DisconnectDeviceChannel) {
  uint on_invalid_count = 0;
  auto on_invalid_cb = [&on_invalid_count]() { on_invalid_count++; };
  display_controller_listener()->InitializeCallbacks(std::move(on_invalid_cb),
                                                     /*displays_changed_cb=*/nullptr,
                                                     /*client_ownership_change_cb=*/nullptr);

  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller_driver()->binding().is_bound());
  RunLoopUntilIdle();
  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller_driver()->binding().is_bound());

  mock_display_controller_driver()->ResetDeviceChannel();
  RunLoopUntilIdle();
  EXPECT_EQ(1u, on_invalid_count);
  EXPECT_FALSE(display_controller_listener()->valid());
}

// Verify that DisplayController becomes invalid when the controller channel is closed.
TEST_F(DisplayControllerListenerTest, DisconnectControllerChannel) {
  uint on_invalid_count = 0;
  auto on_invalid_cb = [&on_invalid_count]() { on_invalid_count++; };
  display_controller_listener()->InitializeCallbacks(std::move(on_invalid_cb),
                                                     /*displays_changed_cb=*/nullptr,
                                                     /*client_ownership_change_cb=*/nullptr);

  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller_driver()->binding().is_bound());
  RunLoopUntilIdle();
  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller_driver()->binding().is_bound());

  mock_display_controller_driver()->ResetControllerBinding();
  RunLoopUntilIdle();
  EXPECT_EQ(1u, on_invalid_count);
  EXPECT_FALSE(display_controller_listener()->valid());
}

// Verify that DisplayController becomes invalid when the controller channel is closed.
TEST_F(DisplayControllerListenerTest, DisconnectControllerAndDeviceChannel) {
  uint on_invalid_count = 0;
  auto on_invalid_cb = [&on_invalid_count]() { on_invalid_count++; };
  display_controller_listener()->InitializeCallbacks(std::move(on_invalid_cb),
                                                     /*displays_changed_cb=*/nullptr,
                                                     /*client_ownership_change_cb=*/nullptr);

  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller_driver()->binding().is_bound());
  RunLoopUntilIdle();
  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller_driver()->binding().is_bound());

  ResetMockDisplayController();
  RunLoopUntilIdle();
  EXPECT_EQ(1u, on_invalid_count);
  EXPECT_FALSE(display_controller_listener()->valid());
}

TEST_F(DisplayControllerListenerTest, DisplaysChanged) {
  std::vector<fuchsia::hardware::display::Info> displays_added;
  std::vector<uint64_t> displays_removed;
  auto displays_changed_cb = [&displays_added, &displays_removed](
                                 std::vector<fuchsia::hardware::display::Info> added,
                                 std::vector<uint64_t> removed) {
    displays_added = added;
    displays_removed = removed;
  };

  display_controller_listener()->InitializeCallbacks(
      /*on_invalid_cb=*/nullptr, std::move(displays_changed_cb),
      /*client_ownership_change_cb=*/nullptr);
  fuchsia::hardware::display::Mode test_mode;
  test_mode.horizontal_resolution = 1024;
  test_mode.vertical_resolution = 800;
  test_mode.refresh_rate_e2 = 60;
  test_mode.flags = 0;
  fuchsia::hardware::display::Info test_display;
  test_display.id = 1;
  test_display.modes = {test_mode};
  test_display.pixel_format = {ZX_PIXEL_FORMAT_ARGB_8888};
  test_display.cursor_configs = {};
  test_display.manufacturer_name = "fake_manufacturer_name";
  test_display.monitor_name = "fake_monitor_name";
  test_display.monitor_serial = "fake_monitor_serial";

  mock_display_controller_driver()->events().DisplaysChanged(/*added=*/{test_display},
                                                             /*removed=*/{2u});
  ASSERT_EQ(0u, displays_added.size());
  ASSERT_EQ(0u, displays_removed.size());
  RunLoopUntilIdle();
  ASSERT_EQ(1u, displays_added.size());
  ASSERT_EQ(1u, displays_removed.size());
  EXPECT_TRUE(fidl::Equals(displays_added[0], test_display));
  EXPECT_EQ(displays_removed[0], 2u);
}

TEST_F(DisplayControllerListenerTest, ClientOwnershipChangeCallback) {
  bool has_ownership = false;
  auto client_ownership_change_cb = [&has_ownership](bool ownership) { has_ownership = ownership; };

  display_controller_listener()->InitializeCallbacks(
      /*on_invalid_cb=*/nullptr, /*displays_changed_cb=*/nullptr,
      std::move(client_ownership_change_cb));

  mock_display_controller_driver()->events().ClientOwnershipChange(true);
  EXPECT_FALSE(has_ownership);
  RunLoopUntilIdle();
  EXPECT_TRUE(has_ownership);
}

TEST_F(DisplayControllerListenerTest, VsyncCallback) {
  uint64_t last_display_id = 0u;
  uint64_t last_timestamp = 0u;
  std::vector<uint64_t> last_images;

  auto vsync_cb = [&](uint64_t display_id, uint64_t timestamp, std::vector<uint64_t> images) {
    last_display_id = display_id;
    last_timestamp = timestamp;
    last_images = std::move(images);
  };
  display_controller_listener()->InitializeCallbacks(/*on_invalid_cb=*/nullptr,
                                                     /*displays_changed_cb=*/nullptr,
                                                     /*client_ownership_change_cb=*/nullptr);
  display_controller_listener()->SetVsyncCallback(std::move(vsync_cb));

  const uint64_t kTestDisplayId = 1u;
  const uint64_t kTestTimestamp = 111111u;
  const uint64_t kTestImageId = 2u;
  mock_display_controller_driver()->events().Vsync(kTestDisplayId, kTestTimestamp, {kTestImageId});
  ASSERT_EQ(0u, last_images.size());
  RunLoopUntilIdle();
  EXPECT_EQ(kTestDisplayId, last_display_id);
  EXPECT_EQ(kTestTimestamp, last_timestamp);
  ASSERT_EQ(1u, last_images.size());
  EXPECT_EQ(last_images[0], kTestImageId);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
