// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/default.h>
#include <lib/async/time.h>
#include <lib/gtest/test_loop_fixture.h>
#include <zircon/syscalls.h>

#include <unordered_set>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/display/display_manager.h"
#include "src/ui/scenic/lib/display/tests/mock_display_controller.h"
#include "src/ui/scenic/lib/gfx/swapchain/display_swapchain.h"

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

class DisplaySwapchainMockTest : public gtest::TestLoopFixture {
 public:
  // |testing::Test|
  void SetUp() override {
    TestLoopFixture::SetUp();

    async_set_default_dispatcher(dispatcher());
    sysmem_ = std::make_unique<Sysmem>();
    display_manager_ = std::make_unique<display::DisplayManager>([]() {});
  }

  // |testing::Test|
  void TearDown() override {
    display_manager_.reset();
    sysmem_.reset();
    TestLoopFixture::TearDown();
  }

  std::unique_ptr<DisplaySwapchain> CreateSwapchain(display::Display* display) {
    auto swapchain = std::make_unique<DisplaySwapchain>(
        sysmem_.get(), display_manager_->default_display_controller(),
        display_manager_->default_display_controller_listener(), display, /*escher*/ nullptr);
    display_manager_->default_display_controller_listener()->SetOnVsyncCallback(
        fit::bind_member(swapchain.get(), &DisplaySwapchain::OnVsync));
    return swapchain;
  }

  display::DisplayManager* display_manager() { return display_manager_.get(); }
  display::Display* display() { return display_manager()->default_display(); }

 private:
  std::unique_ptr<Sysmem> sysmem_;
  std::unique_ptr<display::DisplayManager> display_manager_;
};

TEST_F(DisplaySwapchainMockTest, AcknowledgeVsync) {
  const uint64_t kDisplayId = 0;
  const uint32_t kDisplayWidth = 1024;
  const uint32_t kDisplayHeight = 768;
  const size_t kTotalVsync = 10;
  const size_t kAcknowledgeRate = 5;

  std::unordered_set<uint64_t> cookies_sent;
  size_t num_vsync_swapchain_received = 0;
  size_t num_vsync_acknowledgement = 0;

  auto controller_channel = CreateChannelPair();
  auto device_channel = CreateChannelPair();

  display_manager()->BindDefaultDisplayController(
      fidl::InterfaceHandle<fuchsia::hardware::display::Controller>(
          std::move(controller_channel.client)),
      std::move(device_channel.client));

  display_manager()->SetDefaultDisplayForTests(
      std::make_shared<display::Display>(kDisplayId, kDisplayWidth, kDisplayHeight));

  display::test::MockDisplayController mock_display_controller;
  mock_display_controller.Bind(std::move(device_channel.server),
                               std::move(controller_channel.server));
  mock_display_controller.set_acknowledge_vsync_fn(
      [&cookies_sent, &num_vsync_acknowledgement](uint64_t cookie) {
        ASSERT_TRUE(cookies_sent.find(cookie) != cookies_sent.end());
        ++num_vsync_acknowledgement;
      });

  auto swapchain = CreateSwapchain(display());
  swapchain->RegisterVsyncListener([&num_vsync_swapchain_received](zx::time vsync_timestamp) {
    ++num_vsync_swapchain_received;
  });

  for (size_t vsync_id = 1; vsync_id <= kTotalVsync; vsync_id++) {
    // We only require acknowledgement for every |kAcknowledgeRate| Vsync IDs.
    uint64_t cookie = (vsync_id % kAcknowledgeRate == 0) ? vsync_id : 0;

    test_loop().AdvanceTimeByEpsilon();
    mock_display_controller.events().OnVsync(kDisplayId, /* timestamp */ test_loop().Now().get(),
                                             /* images */ {}, cookie);
    if (cookie) {
      cookies_sent.insert(cookie);
    }

    // Display controller should handle the incoming Vsync message.
    EXPECT_TRUE(RunLoopUntilIdle());
  }

  EXPECT_EQ(num_vsync_swapchain_received, kTotalVsync);
  EXPECT_EQ(num_vsync_acknowledgement, kTotalVsync / kAcknowledgeRate);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
