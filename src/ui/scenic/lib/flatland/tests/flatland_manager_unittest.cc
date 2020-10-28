// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland_manager.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "fuchsia/ui/scenic/internal/cpp/fidl.h"
#include "lib/gtest/real_loop_fixture.h"
#include "src/ui/scenic/lib/flatland/renderer/mocks/mock_buffer_collection_importer.h"
#include "src/ui/scenic/lib/flatland/tests/mock_flatland_presenter.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"

using ::testing::_;
using ::testing::Return;

using flatland::FlatlandManager;
using flatland::FlatlandPresenter;
using flatland::LinkSystem;
using flatland::MockFlatlandPresenter;
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
#define PRESENT(flatland, session_id, expect_success)                                    \
  {                                                                                      \
    const auto num_pending_sessions = GetNumPendingSessionUpdates(session_id);           \
    if (expect_success) {                                                                \
      EXPECT_CALL(*mock_flatland_presenter_, RegisterPresent(session_id, _));            \
      EXPECT_CALL(*mock_flatland_presenter_, ScheduleUpdateForSession(_, _));            \
    }                                                                                    \
    bool processed_callback = false;                                                     \
    flatland->Present(/*requested_presentation_time=*/0, /*acquire_fences=*/{},          \
                      /*release_fences=*/{}, [&](Flatland_Present_Result result) {       \
                        EXPECT_EQ(!expect_success, result.is_err());                     \
                        if (!expect_success) {                                           \
                          EXPECT_EQ(fuchsia::ui::scenic::internal::Error::BAD_OPERATION, \
                                    result.err());                                       \
                        }                                                                \
                        processed_callback = true;                                       \
                      });                                                                \
    /* Wait for the worker thread to process the request. */                             \
    RunLoopUntil([this, session_id, num_pending_sessions] {                              \
      return GetNumPendingSessionUpdates(session_id) > num_pending_sessions;             \
    });                                                                                  \
    EXPECT_TRUE(processed_callback);                                                     \
  }

namespace {

class FlatlandManagerTest : public gtest::RealLoopFixture {
 public:
  FlatlandManagerTest()
      : uber_struct_system_(std::make_shared<UberStructSystem>()),
        link_system_(std::make_shared<LinkSystem>(uber_struct_system_->GetNextInstanceId())) {}

  void SetUp() override {
    gtest::RealLoopFixture::SetUp();

    mock_flatland_presenter_ = new ::testing::StrictMock<MockFlatlandPresenter>();

    ON_CALL(*mock_flatland_presenter_, RegisterPresent(_, _))
        .WillByDefault(::testing::Invoke(
            [&](scheduling::SessionId session_id, std::vector<zx::event> release_fences) {
              EXPECT_TRUE(release_fences.empty());

              const auto next_present_id = scheduling::GetNextPresentId();

              pending_presents_.insert({session_id, next_present_id});

              return next_present_id;
            }));

    ON_CALL(*mock_flatland_presenter_, ScheduleUpdateForSession(_, _))
        .WillByDefault(::testing::Invoke(
            [&](zx::time requested_presentation_time, scheduling::SchedulingIdPair id_pair) {
              // The ID pair must be already registered.
              EXPECT_TRUE(pending_presents_.count(id_pair));

              // Ensure present IDs are strictly increasing.
              auto& queue = pending_session_updates_[id_pair.session_id];
              EXPECT_TRUE(queue.empty() || queue.back() < id_pair.present_id);

              // Save the pending present ID.
              queue.push(id_pair.present_id);
            }));

    flatland_presenter_ = std::shared_ptr<FlatlandPresenter>(mock_flatland_presenter_);

    manager_ = std::make_unique<FlatlandManager>(
        flatland_presenter_, uber_struct_system_, link_system_,
        std::vector<std::shared_ptr<flatland::BufferCollectionImporter>>());
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
    flatland_presenter_.reset();

    gtest::RealLoopFixture::TearDown();
  }

  // Returns the number of currently pending session updates for |session_id|.
  size_t GetNumPendingSessionUpdates(scheduling::SessionId session_id) {
    const auto& queue = pending_session_updates_[session_id];
    return queue.size();
  }

  // Returns the next pending PresentId for |session_id| and removes it from the list of pending
  // session updates. Fails if |session_id| has no pending presents.
  scheduling::PresentId PopPendingPresent(scheduling::SessionId session_id) {
    auto& queue = pending_session_updates_[session_id];
    EXPECT_FALSE(queue.empty());

    auto next_present_id = queue.front();
    queue.pop();
    return next_present_id;
  }

 protected:
  ::testing::StrictMock<MockFlatlandPresenter>* mock_flatland_presenter_;
  const std::shared_ptr<UberStructSystem> uber_struct_system_;

  std::unique_ptr<FlatlandManager> manager_;

  // Storage for |mock_flatland_presenter_|.
  std::set<scheduling::SchedulingIdPair> pending_presents_;
  std::unordered_map<scheduling::SessionId, std::queue<scheduling::PresentId>>
      pending_session_updates_;

 private:
  std::shared_ptr<FlatlandPresenter> flatland_presenter_;
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

TEST_F(FlatlandManagerTest, ManagerImmediatelySendsPresentTokens) {
  // Setup a Flatland instance with an OnPresentTokensReturned() callback.
  fidl::InterfacePtr<fuchsia::ui::scenic::internal::Flatland> flatland;
  manager_->CreateFlatland(flatland.NewRequest());
  const scheduling::SessionId id = uber_struct_system_->GetLatestInstanceId();

  uint32_t returned_tokens = 0;
  flatland.events().OnPresentTokensReturned = [&returned_tokens](uint32_t present_tokens) {
    returned_tokens = present_tokens;
  };

  // Run until the instance receives the initial allotment of tokens.
  RunLoopUntil([&returned_tokens]() { return returned_tokens != 0; });

  EXPECT_EQ(returned_tokens, scheduling::FrameScheduler::kMaxPresentsInFlight - 1u);
}

TEST_F(FlatlandManagerTest, UpdateSessionsReturnsPresentTokens) {
  // Setup two Flatland instances with OnPresentTokensReturned() callbacks.
  fidl::InterfacePtr<fuchsia::ui::scenic::internal::Flatland> flatland1;
  manager_->CreateFlatland(flatland1.NewRequest());
  const scheduling::SessionId id1 = uber_struct_system_->GetLatestInstanceId();

  uint32_t returned_tokens1 = 0;
  flatland1.events().OnPresentTokensReturned = [&returned_tokens1](uint32_t present_tokens) {
    returned_tokens1 = present_tokens;
  };

  fidl::InterfacePtr<fuchsia::ui::scenic::internal::Flatland> flatland2;
  manager_->CreateFlatland(flatland2.NewRequest());
  const scheduling::SessionId id2 = uber_struct_system_->GetLatestInstanceId();

  uint32_t returned_tokens2 = 0;
  flatland2.events().OnPresentTokensReturned = [&returned_tokens2](uint32_t present_tokens) {
    returned_tokens2 = present_tokens;
  };

  // Run both instances receive their initial allotment of tokens, then forget those tokens.
  RunLoopUntil([&returned_tokens1]() { return returned_tokens1 != 0; });
  returned_tokens1 = 0;

  RunLoopUntil([&returned_tokens2]() { return returned_tokens2 != 0; });
  returned_tokens2 = 0;

  // Present both instances twice, but don't update sessions.
  PRESENT(flatland1, id1, true);
  PRESENT(flatland1, id1, true);

  PRESENT(flatland2, id2, true);
  PRESENT(flatland2, id2, true);

  auto snapshot = uber_struct_system_->Snapshot();
  EXPECT_TRUE(snapshot.empty());

  EXPECT_EQ(GetNumPendingSessionUpdates(id1), 2ul);
  EXPECT_EQ(GetNumPendingSessionUpdates(id2), 2ul);

  // Update the first session, but only with the first PresentId, which should push an UberStruct
  // and return one token to the first instance.
  auto next_present_id1 = PopPendingPresent(id1);
  manager_->UpdateSessions({{id1, next_present_id1}}, /*trace_id=*/0);

  snapshot = uber_struct_system_->Snapshot();
  EXPECT_EQ(snapshot.size(), 1u);
  EXPECT_TRUE(snapshot.count(id1));
  EXPECT_FALSE(snapshot.count(id2));

  RunLoopUntil([&returned_tokens1]() { return returned_tokens1 != 0; });

  EXPECT_EQ(returned_tokens1, 1u);
  EXPECT_EQ(returned_tokens2, 0u);

  EXPECT_EQ(GetNumPendingSessionUpdates(id1), 1ul);
  EXPECT_EQ(GetNumPendingSessionUpdates(id2), 2ul);

  returned_tokens1 = 0;

  // Update only the second session and consume both PresentIds, which should push an UberStruct
  // and return two tokens to the second instance.
  auto next_present_id2 = PopPendingPresent(id2);
  next_present_id2 = PopPendingPresent(id2);

  manager_->UpdateSessions({{id2, next_present_id2}}, /*trace_id=*/0);

  snapshot = uber_struct_system_->Snapshot();
  EXPECT_EQ(snapshot.size(), 2u);
  EXPECT_TRUE(snapshot.count(id1));
  EXPECT_TRUE(snapshot.count(id2));

  RunLoopUntil([&returned_tokens2]() { return returned_tokens2 != 0; });

  EXPECT_EQ(returned_tokens1, 0u);
  EXPECT_EQ(returned_tokens2, 2u);

  EXPECT_EQ(GetNumPendingSessionUpdates(id1), 1ul);
  EXPECT_EQ(GetNumPendingSessionUpdates(id2), 0ul);
}

#undef PRESENT

}  // namespace test
}  // namespace flatland
