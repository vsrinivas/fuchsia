// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

// clang-format off
#include "test/base.h"
#include "test/fidl_client.h"

// These must be included after base.h and fidl_client.h because the Banjo bindings use #defines
// that conflict with enum names in the FIDL bindings.
#include "../../fake/fake-display.h"
#include "../controller.h"
#include "../client.h"
// clang-format on

namespace sysmem = ::llcpp::fuchsia::sysmem;

namespace display {

class IntegrationTest : public TestBase {
 public:
  fbl::RefPtr<display::DisplayInfo> display_info(uint64_t id) __TA_REQUIRES(controller()->mtx()) {
    auto iter = controller()->displays_.find(id);
    if (iter.IsValid()) {
      return iter.CopyPointer();
    } else {
      return nullptr;
    }
  }

  bool primary_client_connected() {
    fbl::AutoLock l(controller()->mtx());
    return (controller()->primary_client_ && controller()->primary_client_->enable_vsync_ &&
            controller()->primary_client_ == controller()->active_client_);
  }

  bool primary_client_dead() {
    fbl::AutoLock l(controller()->mtx());
    return controller()->primary_client_ == nullptr;
  }

  // |TestBase|
  void SetUp() override {
    TestBase::SetUp();
    zx::channel client, server;
    EXPECT_OK(zx::channel::create(0, &client, &server));
    auto connector =
        std::make_unique<sysmem::DriverConnector::SyncClient>(zx::channel(sysmem_fidl()->get()));
    EXPECT_TRUE(connector->Connect(std::move(server)).ok());
    __UNUSED auto c = connector->mutable_channel()->release();
    sysmem_ = std::make_unique<sysmem::Allocator::SyncClient>(std::move(client));
  }

  std::unique_ptr<sysmem::Allocator::SyncClient> sysmem_;
};

TEST_F(IntegrationTest, ClientsCanBail) {
  auto client = std::make_unique<TestFidlClient>(sysmem_.get());
  ASSERT_TRUE(client->CreateChannel(display_fidl()->get(), false));
  ASSERT_TRUE(client->Bind(dispatcher()));
  ASSERT_EQ(ZX_OK, client->PresentImage());
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([this]() { return primary_client_connected(); }));
  display()->SendVsync();
  client.reset();

  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([this]() { return primary_client_dead(); }));
}

TEST_F(IntegrationTest, MustUseUniqueEvenIDs) {
  TestFidlClient client(sysmem_.get());
  ASSERT_TRUE(client.CreateChannel(display_fidl()->get(), false));
  ASSERT_TRUE(client.Bind(dispatcher()));
  zx::event event_a, event_b, event_c;
  ASSERT_OK(zx::event::create(0, &event_a));
  ASSERT_OK(zx::event::create(0, &event_b));
  ASSERT_OK(zx::event::create(0, &event_c));
  EXPECT_OK(client.dc_->ImportEvent(std::move(event_a), 123).status());
  // ImportEvent is one way. Expect the next call to fail.
  EXPECT_OK(client.dc_->ImportEvent(std::move(event_b), 123).status());
  // This test passes if it closes without deadlocking.
  // TODO: Use LLCPP epitaphs when available to detect ZX_ERR_PEER_CLOSED.
}

TEST_F(IntegrationTest, SendVsyncsAfterEmptyConfig) {
  TestFidlClient vc_client(sysmem_.get());
  ASSERT_TRUE(vc_client.CreateChannel(display_fidl()->get(), /*is_vc=*/true));
  EXPECT_EQ(ZX_OK, vc_client.dc_->SetDisplayLayers(1, {}).status());
  EXPECT_EQ(ZX_OK, vc_client.dc_->ApplyConfig().status());
  // vc_client.PresentImage();

  auto primary_client = std::make_unique<TestFidlClient>(sysmem_.get());
  ASSERT_TRUE(primary_client->CreateChannel(display_fidl()->get(), /*is_vc=*/false));
  ASSERT_TRUE(primary_client->Bind(dispatcher()));
  EXPECT_TRUE(
      RunLoopWithTimeoutOrUntil([this]() { return primary_client_connected(); }, zx::sec(1)));

  // Present an image
  EXPECT_OK(primary_client->PresentImage());
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [this, id = primary_client->display_id()]() {
        fbl::AutoLock lock(controller()->mtx());
        auto info = display_info(id);
        return info->vsync_layer_count == 1;
      },
      zx::sec(1)));
  display()->SendVsync();
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [p = primary_client.get()]() { return p->vsync_count_ > 0; }, zx::sec(1)));

  // Set an empty config
  EXPECT_OK(primary_client->dc_->SetDisplayLayers(primary_client->display_id(), {}).status());
  EXPECT_OK(primary_client->dc_->ApplyConfig().status());
  // Wait for it to apply
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [this, id = primary_client->displays_[0].id_]() {
        fbl::AutoLock lock(controller()->mtx());
        auto info = display_info(id);
        return info->vsync_layer_count == 0;
      },
      zx::sec(1)));

  // The old client disconnects
  primary_client.reset();
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([this]() { return primary_client_dead(); }));

  // A new client connects
  primary_client = std::make_unique<TestFidlClient>(sysmem_.get());
  ASSERT_TRUE(primary_client->CreateChannel(display_fidl()->get(), /*is_vc=*/false));
  ASSERT_TRUE(primary_client->Bind(dispatcher()));
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([this]() { return primary_client_connected(); }));
  // ... and presents before the previous client's empty vsync
  EXPECT_EQ(ZX_OK, primary_client->PresentImage());
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [this, id = primary_client->display_id()]() {
        fbl::AutoLock lock(controller()->mtx());
        auto info = display_info(id);
        return info->vsync_layer_count == 1;
      },
      zx::sec(1)));

  // Empty vsync for last client. Nothing should be sent to the new client.
  controller()->DisplayControllerInterfaceOnDisplayVsync(primary_client->display_id(), 0u, nullptr,
                                                         0);

  // Send a second vsync, using the config the client applied.
  display()->SendVsync();
  auto count = primary_client->vsync_count_;
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [c = count, p = primary_client.get()]() { return p->vsync_count_ > c; }, zx::sec(1)));
}

}  // namespace display
