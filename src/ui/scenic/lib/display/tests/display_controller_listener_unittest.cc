// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/display_controller_listener.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/zx/channel.h>
#include <zircon/pixelformat.h>

#include <gtest/gtest.h>

#include "lib/fidl/cpp/comparison.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/display/tests/mock_display_controller.h"

namespace scenic_impl {
namespace display {
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

    mock_display_controller_ = std::make_unique<MockDisplayController>();
    mock_display_controller_->Bind(std::move(device_channel.server),
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

  void ResetMockDisplayController() { mock_display_controller_.reset(); }
  void ResetDisplayControllerListener() { display_controller_listener_.reset(); }

  MockDisplayController* mock_display_controller() { return mock_display_controller_.get(); }

 private:
  std::unique_ptr<MockDisplayController> mock_display_controller_;
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
                                       controller_handle);

    EXPECT_FALSE(listener.valid());
  }
}

// Verify that DisplayController connects to the FIDL service.
TEST_F(DisplayControllerListenerTest, Connect) {
  display_controller_listener()->InitializeCallbacks(/*on_invalid_cb=*/nullptr,
                                                     /*displays_changed_cb=*/nullptr,
                                                     /*client_ownership_change_cb=*/nullptr);

  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller()->binding().is_bound());
  RunLoopUntilIdle();
  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller()->binding().is_bound());
}

// Verify that DisplayController becomes invalid when the device channel is closed.
TEST_F(DisplayControllerListenerTest, DisconnectDeviceChannel) {
  uint on_invalid_count = 0;
  auto on_invalid_cb = [&on_invalid_count]() { on_invalid_count++; };
  display_controller_listener()->InitializeCallbacks(std::move(on_invalid_cb),
                                                     /*displays_changed_cb=*/nullptr,
                                                     /*client_ownership_change_cb=*/nullptr);

  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller()->binding().is_bound());
  RunLoopUntilIdle();
  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller()->binding().is_bound());

  mock_display_controller()->ResetDeviceChannel();
  RunLoopUntilIdle();
  EXPECT_EQ(1u, on_invalid_count);
  EXPECT_FALSE(display_controller_listener()->valid());

  // Expect no crashes on teardown.
  ResetDisplayControllerListener();
  RunLoopUntilIdle();
}

// Verify that DisplayController becomes invalid when the controller channel is closed.
TEST_F(DisplayControllerListenerTest, DisconnectControllerChannel) {
  uint on_invalid_count = 0;
  auto on_invalid_cb = [&on_invalid_count]() { on_invalid_count++; };
  display_controller_listener()->InitializeCallbacks(std::move(on_invalid_cb),
                                                     /*displays_changed_cb=*/nullptr,
                                                     /*client_ownership_change_cb=*/nullptr);

  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller()->binding().is_bound());
  RunLoopUntilIdle();
  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller()->binding().is_bound());

  mock_display_controller()->ResetControllerBinding();
  RunLoopUntilIdle();
  EXPECT_EQ(1u, on_invalid_count);
  EXPECT_FALSE(display_controller_listener()->valid());

  // Expect no crashes on teardown.
  ResetDisplayControllerListener();
  RunLoopUntilIdle();
}

// Verify that DisplayController becomes invalid when the controller channel is closed, but that we
// don't receive a callback after ClearCallbacks.
TEST_F(DisplayControllerListenerTest, DisconnectControllerChannelAfterClearCallbacks) {
  uint on_invalid_count = 0;
  auto on_invalid_cb = [&on_invalid_count]() { on_invalid_count++; };
  display_controller_listener()->InitializeCallbacks(std::move(on_invalid_cb),
                                                     /*displays_changed_cb=*/nullptr,
                                                     /*client_ownership_change_cb=*/nullptr);

  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller()->binding().is_bound());
  RunLoopUntilIdle();
  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller()->binding().is_bound());
  display_controller_listener()->ClearCallbacks();
  mock_display_controller()->ResetControllerBinding();
  RunLoopUntilIdle();
  EXPECT_EQ(0u, on_invalid_count);
  EXPECT_FALSE(display_controller_listener()->valid());
}

// Verify that DisplayController becomes invalid when the device and controller channel is closed,
// and that we don't get the callback twice.
TEST_F(DisplayControllerListenerTest, DisconnectControllerAndDeviceChannel) {
  uint on_invalid_count = 0;
  auto on_invalid_cb = [&on_invalid_count]() { on_invalid_count++; };
  display_controller_listener()->InitializeCallbacks(std::move(on_invalid_cb),
                                                     /*displays_changed_cb=*/nullptr,
                                                     /*client_ownership_change_cb=*/nullptr);

  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller()->binding().is_bound());
  RunLoopUntilIdle();
  EXPECT_TRUE(display_controller_listener()->valid());
  EXPECT_TRUE(mock_display_controller()->binding().is_bound());

  ResetMockDisplayController();
  RunLoopUntilIdle();
  EXPECT_EQ(1u, on_invalid_count);
  EXPECT_FALSE(display_controller_listener()->valid());

  // Expect no crashes on teardown.
  ResetDisplayControllerListener();
  RunLoopUntilIdle();
}

TEST_F(DisplayControllerListenerTest, OnDisplaysChanged) {
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

  mock_display_controller()->events().OnDisplaysChanged(/*added=*/{test_display},
                                                        /*removed=*/{2u});
  ASSERT_EQ(0u, displays_added.size());
  ASSERT_EQ(0u, displays_removed.size());
  RunLoopUntilIdle();
  ASSERT_EQ(1u, displays_added.size());
  ASSERT_EQ(1u, displays_removed.size());
  EXPECT_TRUE(fidl::Equals(displays_added[0], test_display));
  EXPECT_EQ(displays_removed[0], 2u);

  // Verify we stop getting callbacks after ClearCallbacks().
  display_controller_listener()->ClearCallbacks();
  mock_display_controller()->events().OnDisplaysChanged(/*added=*/{},
                                                        /*removed=*/{3u});
  RunLoopUntilIdle();

  // Expect that nothing changed.
  ASSERT_EQ(1u, displays_added.size());
  ASSERT_EQ(1u, displays_removed.size());
  EXPECT_EQ(displays_removed[0], 2u);

  // Expect no crashes on teardown.
  ResetDisplayControllerListener();
  RunLoopUntilIdle();
}

TEST_F(DisplayControllerListenerTest, OnClientOwnershipChangeCallback) {
  bool has_ownership = false;
  auto client_ownership_change_cb = [&has_ownership](bool ownership) { has_ownership = ownership; };

  display_controller_listener()->InitializeCallbacks(
      /*on_invalid_cb=*/nullptr, /*displays_changed_cb=*/nullptr,
      std::move(client_ownership_change_cb));

  mock_display_controller()->events().OnClientOwnershipChange(true);
  EXPECT_FALSE(has_ownership);
  RunLoopUntilIdle();
  EXPECT_TRUE(has_ownership);

  // Verify we stop getting callbacks after ClearCallbacks().
  display_controller_listener()->ClearCallbacks();
  mock_display_controller()->events().OnClientOwnershipChange(false);
  RunLoopUntilIdle();
  // Expect that nothing changed.
  EXPECT_TRUE(has_ownership);

  // Expect no crashes on teardown.
  ResetDisplayControllerListener();
  RunLoopUntilIdle();
}

TEST_F(DisplayControllerListenerTest, OnVsyncCallback) {
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
  display_controller_listener()->SetOnVsyncCallback(std::move(vsync_cb));

  const uint64_t kTestDisplayId = 1u;
  const uint64_t kTestTimestamp = 111111u;
  const uint64_t kTestImageId = 2u;
  mock_display_controller()->events().OnVsync(kTestDisplayId, kTestTimestamp, {kTestImageId});
  ASSERT_EQ(0u, last_images.size());
  RunLoopUntilIdle();
  EXPECT_EQ(kTestDisplayId, last_display_id);
  EXPECT_EQ(kTestTimestamp, last_timestamp);
  ASSERT_EQ(1u, last_images.size());
  EXPECT_EQ(last_images[0], kTestImageId);

  // Verify we stop getting callbacks after ClearCallbacks().
  display_controller_listener()->ClearCallbacks();
  mock_display_controller()->events().OnVsync(kTestDisplayId + 1, kTestTimestamp, {kTestImageId});
  // Expect that nothing changed.
  RunLoopUntilIdle();
  EXPECT_EQ(kTestDisplayId, last_display_id);

  // Expect no crashes on teardown.
  ResetDisplayControllerListener();
  RunLoopUntilIdle();
}

}  // namespace test
}  // namespace display
}  // namespace scenic_impl
