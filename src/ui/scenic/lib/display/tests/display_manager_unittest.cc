// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/display_manager.h"

#include <lib/async/default.h>
#include <lib/async/time.h>

#include <unordered_set>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
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

class DisplayManagerMockTest : public gtest::TestLoopFixture {
 public:
  // |testing::Test|
  void SetUp() override {
    TestLoopFixture::SetUp();

    async_set_default_dispatcher(dispatcher());
    display_manager_ = std::make_unique<display::DisplayManager>([]() {});
  }

  // |testing::Test|
  void TearDown() override {
    display_manager_.reset();
    TestLoopFixture::TearDown();
  }

  display::DisplayManager* display_manager() { return display_manager_.get(); }
  display::Display* display() { return display_manager()->default_display(); }

 private:
  std::unique_ptr<display::DisplayManager> display_manager_;
};

TEST_F(DisplayManagerMockTest, DisplayVsyncCallback) {
  const uint64_t kDisplayId = 0;
  const uint32_t kDisplayWidth = 1024;
  const uint32_t kDisplayHeight = 768;
  const size_t kTotalVsync = 10;
  const size_t kAcknowledgeRate = 5;

  std::unordered_set<uint64_t> cookies_sent;
  size_t num_vsync_display_received = 0;
  size_t num_vsync_acknowledgement = 0;

  auto controller_channel = CreateChannelPair();

  display_manager()->BindDefaultDisplayController(
      fidl::InterfaceHandle<fuchsia::hardware::display::Controller>(
          std::move(controller_channel.client)));

  display_manager()->SetDefaultDisplayForTests(
      std::make_shared<display::Display>(kDisplayId, kDisplayWidth, kDisplayHeight));

  display::test::MockDisplayController mock_display_controller;
  mock_display_controller.Bind(std::move(controller_channel.server));
  mock_display_controller.set_acknowledge_vsync_fn(
      [&cookies_sent, &num_vsync_acknowledgement](uint64_t cookie) {
        ASSERT_TRUE(cookies_sent.find(cookie) != cookies_sent.end());
        ++num_vsync_acknowledgement;
      });

  display_manager()->default_display()->SetVsyncCallback(
      [&num_vsync_display_received](zx::time timestamp,
                                    fuchsia::hardware::display::ConfigStamp stamp) {
        ++num_vsync_display_received;
      });

  for (size_t vsync_id = 1; vsync_id <= kTotalVsync; vsync_id++) {
    // We only require acknowledgement for every |kAcknowledgeRate| Vsync IDs.
    uint64_t cookie = (vsync_id % kAcknowledgeRate == 0) ? vsync_id : 0;

    test_loop().AdvanceTimeByEpsilon();
    mock_display_controller.events().OnVsync(kDisplayId, /* timestamp */ test_loop().Now().get(),
                                             {.value = 1u}, cookie);
    if (cookie) {
      cookies_sent.insert(cookie);
    }

    // Display controller should handle the incoming Vsync message.
    EXPECT_TRUE(RunLoopUntilIdle());
  }

  EXPECT_EQ(num_vsync_display_received, kTotalVsync);
  EXPECT_EQ(num_vsync_acknowledgement, kTotalVsync / kAcknowledgeRate);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
