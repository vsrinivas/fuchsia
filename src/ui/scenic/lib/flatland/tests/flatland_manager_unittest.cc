// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland_manager.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "fuchsia/ui/scenic/internal/cpp/fidl.h"
#include "lib/gtest/real_loop_fixture.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/flatland/tests/mock_flatland_presenter.h"
#include "src/ui/scenic/lib/flatland/tests/mock_renderer.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"

using ::testing::_;
using ::testing::Return;

using flatland::FlatlandManager;
using flatland::FlatlandPresenter;
using flatland::LinkSystem;
using flatland::MockFlatlandPresenter;
using flatland::MockRenderer;
using flatland::Renderer;
using flatland::UberStructSystem;
using fuchsia::ui::scenic::internal::Flatland;
using fuchsia::ui::scenic::internal::Flatland_Present_Result;

// These macros works like functions that check a variety of conditions, but if those conditions
// fail, the line number for the failure will appear in-line rather than in a function.

// This macro calls Present() on a Flatland object and immediately triggers the session update
// for all sessions so that changes from that Present() are visible in global systems. This is
// primarily useful for testing the user-facing Flatland API.
//
// This macro must be used within a test using the FlatlandManagerTest harness.
//
// |flatland| is a Flatland object constructed with the MockFlatlandPresenter owned by the
// FlatlandManagerTest harness. |session_id| is the SessionId for |flatland|. |expect_success|
// should be false if the call to Present() is expected to trigger an error.
#define PRESENT(flatland, session_id, expect_success)                                           \
  {                                                                                             \
    bool processed_callback = false;                                                            \
    flatland->Present(/*requested_presentation_time=*/0, /*acquire_fences=*/{},                 \
                      /*release_fences=*/{}, [&](Flatland_Present_Result result) {              \
                        EXPECT_EQ(!expect_success, result.is_err());                            \
                        if (expect_success) {                                                   \
                          EXPECT_EQ(1u, result.response().num_presents_remaining);              \
                        } else {                                                                \
                          EXPECT_EQ(fuchsia::ui::scenic::internal::Error::BAD_OPERATION,        \
                                    result.err());                                              \
                        }                                                                       \
                        processed_callback = true;                                              \
                      });                                                                       \
    /* Wait for the worker thread to process the request. */                                    \
    RunLoopUntil(                                                                               \
        [this, session_id] { return mock_flatland_presenter_->HasSessionUpdate(session_id); }); \
    /* Trigger the Present callback on the test looper. */                                      \
    RunLoopUntilIdle();                                                                         \
    EXPECT_TRUE(processed_callback);                                                            \
    mock_flatland_presenter_->ApplySessionUpdatesAndSignalFences();                             \
  }

namespace {

class FlatlandManagerTest : public gtest::RealLoopFixture {
 public:
  FlatlandManagerTest()
      : uber_struct_system_(std::make_shared<UberStructSystem>()),
        link_system_(std::make_shared<LinkSystem>(uber_struct_system_->GetNextInstanceId())) {}

  void SetUp() override {
    gtest::RealLoopFixture::SetUp();

    mock_flatland_presenter_ = new MockFlatlandPresenter(uber_struct_system_.get());
    flatland_presenter_ = std::shared_ptr<FlatlandPresenter>(mock_flatland_presenter_);

    mock_renderer_ = new MockRenderer();
    renderer_ = std::shared_ptr<Renderer>(mock_renderer_);

    manager_ = std::make_unique<FlatlandManager>(flatland_presenter_, renderer_,
                                                 uber_struct_system_, link_system_);
  }

  void TearDown() override {
    // Triggers cleanup of manager resources for Flatland instances that have exited.
    RunLoopUntilIdle();

    // |manager_| may have been reset during the test.
    if (manager_) {
      EXPECT_EQ(manager_->GetSessionCount(), 0ul);
    }

    auto snapshot = uber_struct_system_->Snapshot();
    EXPECT_TRUE(snapshot.empty());

    manager_.reset();
    renderer_.reset();
    flatland_presenter_.reset();

    gtest::RealLoopFixture::TearDown();
  }

 protected:
  MockFlatlandPresenter* mock_flatland_presenter_;
  MockRenderer* mock_renderer_;
  const std::shared_ptr<UberStructSystem> uber_struct_system_;

  std::unique_ptr<FlatlandManager> manager_;

 private:
  std::shared_ptr<FlatlandPresenter> flatland_presenter_;
  std::shared_ptr<Renderer> renderer_;
  const std::shared_ptr<LinkSystem> link_system_;
};

}  // namespace

namespace flatland {
namespace test {

TEST_F(FlatlandManagerTest, CreateFlatlands) {
  fidl::InterfacePtr<fuchsia::ui::scenic::internal::Flatland> flatland1;
  manager_->CreateFlatland(flatland1.NewRequest());

  fidl::InterfacePtr<fuchsia::ui::scenic::internal::Flatland> flatland2;
  manager_->CreateFlatland(flatland2.NewRequest());

  RunLoopUntilIdle();

  EXPECT_TRUE(flatland1.is_bound());
  EXPECT_TRUE(flatland2.is_bound());

  EXPECT_EQ(manager_->GetSessionCount(), 2ul);
}

TEST_F(FlatlandManagerTest, ManagerDiesBeforeClients) {
  fidl::InterfacePtr<fuchsia::ui::scenic::internal::Flatland> flatland;
  manager_->CreateFlatland(flatland.NewRequest());

  RunLoopUntilIdle();

  EXPECT_TRUE(flatland.is_bound());
  EXPECT_EQ(manager_->GetSessionCount(), 1ul);

  // Explicitly kill the server.
  manager_.reset();

  RunLoopUntilIdle();

  EXPECT_FALSE(flatland.is_bound());
}

TEST_F(FlatlandManagerTest, FlatlandsPublishToSharedUberStructSystem) {
  fidl::InterfacePtr<fuchsia::ui::scenic::internal::Flatland> flatland1;
  manager_->CreateFlatland(flatland1.NewRequest());
  const scheduling::SessionId id1 = uber_struct_system_->GetLatestInstanceId();

  fidl::InterfacePtr<fuchsia::ui::scenic::internal::Flatland> flatland2;
  manager_->CreateFlatland(flatland2.NewRequest());
  const scheduling::SessionId id2 = uber_struct_system_->GetLatestInstanceId();

  RunLoopUntilIdle();

  // Both instances publish to the shared UberStructSystem.
  PRESENT(flatland1, id1, true);
  PRESENT(flatland2, id2, true);

  auto snapshot = uber_struct_system_->Snapshot();
  EXPECT_EQ(snapshot.size(), 2ul);
}

#undef PRESENT

}  // namespace test
}  // namespace flatland
