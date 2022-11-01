// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest_manager/memory_pressure_handler.h"

#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <src/lib/testing/loop_fixture/test_loop_fixture.h>
#include <virtio/balloon.h>
#include <virtio/virtio_ids.h>

namespace {

const uint64_t kTestAvailMemSize = static_cast<uint64_t>(PAGE_SIZE) * 1182938;
const uint32_t kExpectedInflateNumPages =
    kTestAvailMemSize / PAGE_SIZE / 100 *
    MemoryPressureHandler::kBalloonAvailableMemoryInflatePercentage;

static_assert(
    kTestAvailMemSize > std::numeric_limits<uint32_t>::max(),
    "Available memory should not fit into uint32_t to validate math in the memory pressure handler");

class FakeBalloonController : public fuchsia::virtualization::BalloonController {
 public:
  void GetBalloonSize(GetBalloonSizeCallback callback) override {
    callback(current_num_pages_, target_num_pages);
  }

  void RequestNumPages(uint32_t requested_num_pages) override {
    target_num_pages = requested_num_pages;
  }

  void GetMemStats(GetMemStatsCallback callback) override {
    std::vector<::fuchsia::virtualization::MemStat> mem_stats;
    mem_stats.push_back({VIRTIO_BALLOON_S_MEMFREE, free_memory_});
    mem_stats.push_back({VIRTIO_BALLOON_S_AVAIL, available_memory_});
    return callback(ZX_OK, mem_stats);
  }

  uint32_t current_num_pages_ = 0;
  uint32_t target_num_pages = 0;
  uint64_t available_memory_ = kTestAvailMemSize;
  uint64_t free_memory_ = kTestAvailMemSize / 2;
};

class FakeGuest : public fuchsia::virtualization::testing::Guest_TestBase {
 public:
  FakeGuest(sys::testing::ComponentContextProvider* provider,
            FakeBalloonController* balloon_controller)
      : balloon_controller_binding_(balloon_controller) {
    FX_CHECK(ZX_OK ==
             provider->service_directory_provider()->AddService(bindings_.GetHandler(this)));
  }

  // |fuchsia::virtualization::Guest|
  void GetBalloonController(
      ::fidl::InterfaceRequest<::fuchsia::virtualization::BalloonController> controller,
      GetBalloonControllerCallback callback) override {
    balloon_controller_binding_.Bind(std::move(controller));
  }

 private:
  void NotImplemented_(const std::string& name) override {
    ADD_FAILURE() << "unexpected message received: " << name;
  }

  fidl::BindingSet<fuchsia::virtualization::Guest> bindings_;
  fidl::Binding<fuchsia::virtualization::BalloonController> balloon_controller_binding_;
};

class FakeMemoryPressureProvider : public fuchsia::memorypressure::Provider {
 public:
  explicit FakeMemoryPressureProvider(sys::testing::ComponentContextProvider* provider) {
    FX_CHECK(ZX_OK ==
             provider->service_directory_provider()->AddService(bindings_.GetHandler(this)));
  }

  // |fuchsia::memorypressure::Provider|
  void RegisterWatcher(
      ::fidl::InterfaceHandle<::fuchsia::memorypressure::Watcher> watcher) override {
    watcher_ = watcher.Bind();
  }

  void SetMemoryPressureLevel(fuchsia::memorypressure::Level level) {
    watcher_->OnLevelChanged(level, []() {});
  }

 private:
  fuchsia::memorypressure::WatcherPtr watcher_;
  fidl::BindingSet<fuchsia::memorypressure::Provider> bindings_;
};

class MemoryPressureHandlerTest : public gtest::TestLoopFixture {
 public:
  MemoryPressureHandlerTest()
      : context_provider_(dispatcher()), memory_pressure_handler_(dispatcher()) {}

  void SetUp() override {
    TestLoopFixture::SetUp();
    fake_memory_pressure_provider_ =
        std::make_unique<FakeMemoryPressureProvider>(&context_provider_);
    fake_balloon_controller_ = std::make_unique<FakeBalloonController>();
    fake_guest_ = std::make_unique<FakeGuest>(&context_provider_, fake_balloon_controller_.get());
    memory_pressure_handler_.Start(context_provider_.context());
    RunLoopUntilIdle();
  }
  sys::testing::ComponentContextProvider context_provider_;
  MemoryPressureHandler memory_pressure_handler_;
  std::unique_ptr<FakeMemoryPressureProvider> fake_memory_pressure_provider_;
  std::unique_ptr<FakeBalloonController> fake_balloon_controller_;
  std::unique_ptr<FakeGuest> fake_guest_;
};

TEST_F(MemoryPressureHandlerTest, InflateOnWarningDeflateScheduled) {
  fake_memory_pressure_provider_->SetMemoryPressureLevel(fuchsia::memorypressure::Level::WARNING);
  RunLoopUntilIdle();
  EXPECT_EQ(fake_balloon_controller_->target_num_pages, kExpectedInflateNumPages);
  fake_memory_pressure_provider_->SetMemoryPressureLevel(fuchsia::memorypressure::Level::NORMAL);
  RunLoopUntilIdle();
  // Balloon deflate got scheduled, it would not happen at this point
  EXPECT_EQ(fake_balloon_controller_->target_num_pages, kExpectedInflateNumPages);
  // Wait for inflate operation to complete, deflate would happen after wait
  RunLoopFor(MemoryPressureHandler::kBalloonInflateCompletionWaitTime);
  EXPECT_EQ(fake_balloon_controller_->target_num_pages, 0u);
}

TEST_F(MemoryPressureHandlerTest, InflateOnWarningDeflateAfterWait) {
  fake_memory_pressure_provider_->SetMemoryPressureLevel(fuchsia::memorypressure::Level::WARNING);
  RunLoopUntilIdle();
  EXPECT_EQ(fake_balloon_controller_->target_num_pages, kExpectedInflateNumPages);
  RunLoopFor(MemoryPressureHandler::kBalloonInflateCompletionWaitTime);
  fake_memory_pressure_provider_->SetMemoryPressureLevel(fuchsia::memorypressure::Level::NORMAL);
  RunLoopUntilIdle();
  EXPECT_EQ(fake_balloon_controller_->target_num_pages, 0u);
}

TEST_F(MemoryPressureHandlerTest, InflateOnWarningThenCriticalWhichIsIgnored) {
  fake_memory_pressure_provider_->SetMemoryPressureLevel(fuchsia::memorypressure::Level::WARNING);
  RunLoopUntilIdle();
  EXPECT_EQ(fake_balloon_controller_->target_num_pages, kExpectedInflateNumPages);
  fake_memory_pressure_provider_->SetMemoryPressureLevel(fuchsia::memorypressure::Level::CRITICAL);
  RunLoopUntilIdle();
  EXPECT_EQ(fake_balloon_controller_->target_num_pages, kExpectedInflateNumPages);
}

TEST_F(MemoryPressureHandlerTest, InflateOnWarningScheduleDeflateAndBackToWarning) {
  fake_memory_pressure_provider_->SetMemoryPressureLevel(fuchsia::memorypressure::Level::WARNING);
  RunLoopFor(zx::msec(100));
  EXPECT_EQ(fake_balloon_controller_->target_num_pages, kExpectedInflateNumPages);
  // Deflate is scheduled
  fake_memory_pressure_provider_->SetMemoryPressureLevel(fuchsia::memorypressure::Level::NORMAL);
  RunLoopUntilIdle();
  EXPECT_EQ(fake_balloon_controller_->target_num_pages, kExpectedInflateNumPages);
  // Scheduled deflate will be ignored because we switched back to warning
  fake_memory_pressure_provider_->SetMemoryPressureLevel(fuchsia::memorypressure::Level::WARNING);
  RunLoopFor(zx::hour(1));
  EXPECT_EQ(fake_balloon_controller_->target_num_pages, kExpectedInflateNumPages);
}

}  // namespace
