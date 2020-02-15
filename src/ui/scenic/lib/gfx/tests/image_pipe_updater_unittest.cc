// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/image_pipe_updater.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/gtest/test_loop_fixture.h"
#include "src/ui/scenic/lib/gfx/resources/image_pipe_base.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/mocks.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/util.h"
#include "src/ui/scenic/lib/scheduling/tests/mocks/frame_scheduler_mocks.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class MockImagePipe : public ImagePipeBase {
 public:
  MockImagePipe(Session* session)
      : ImagePipeBase(session, 1,
                      {ResourceType::kImagePipe | ResourceType::kImageBase, "ImagePipe"}),
        weak_factory_(this) {}

  ImagePipeUpdateResults Update(scheduling::PresentId present_id) override {
    ++update_called_count_;
    return {.image_updated = true};
  }

  void UpdateEscherImage(escher::BatchGpuUploader* gpu_uploader,
                         escher::ImageLayoutUpdater* layout_uploader) override {}
  const escher::ImagePtr& GetEscherImage() override { return null_; }
  bool use_protected_memory() override { return false; }

  int64_t update_called_count_ = 0;

  escher::ImagePtr null_;
  fxl::WeakPtrFactory<MockImagePipe> weak_factory_;  // must be last
};

class ImagePipeUpdaterTest : public ::gtest::TestLoopFixture {
 public:
  // | ::testing::Test |
  void SetUp() override {
    scheduler_ = std::make_shared<scheduling::test::MockFrameScheduler>();
    scheduler_->set_schedule_update_for_session_callback(
        [this](auto...) { ++schedule_call_count_; });
    release_fence_signaller_ = std::make_unique<ReleaseFenceSignallerForTest>();
    image_pipe_updater_ = std::make_unique<ImagePipeUpdater>(scheduler_);
    SessionContext context{};
    session_ = std::make_unique<gfx::Session>(1, context);
    image_pipe_ = fxl::MakeRefCounted<MockImagePipe>(session_.get());
  }

  // | ::testing::Test |
  void TearDown() override {
    scheduler_.reset();
    image_pipe_updater_.reset();
    release_fence_signaller_.reset();
    image_pipe_.reset();
    session_.reset();
  }

  int64_t schedule_call_count_ = 0;
  int64_t remove_session_call_count_ = 0;

  std::shared_ptr<scheduling::test::MockFrameScheduler> scheduler_;
  std::unique_ptr<ImagePipeUpdater> image_pipe_updater_;
  fxl::RefPtr<MockImagePipe> image_pipe_;
  std::unique_ptr<gfx::Session> session_;
  std::unique_ptr<ReleaseFenceSignallerForTest> release_fence_signaller_;
};

TEST_F(ImagePipeUpdaterTest, ScheduleWithNoFences_ShouldScheduleOnLoop) {
  image_pipe_updater_->ScheduleImagePipeUpdate(
      /*presentation_time=*/zx::time(0), image_pipe_->weak_factory_.GetWeakPtr(),
      /*acquire_fences=*/{}, /*release_fences=*/{}, /*callback=*/[](auto...) {});

  RunLoopUntilIdle();
  EXPECT_EQ(schedule_call_count_, 1);
}

TEST_F(ImagePipeUpdaterTest, ScheduleWithFences_ShouldScheduleOnLoop_WhenAllFencesSignaled) {
  std::vector<zx::event> acquire_fences = CreateEventArray(2);
  zx::event fence1 = CopyEvent(acquire_fences.at(0));
  zx::event fence2 = CopyEvent(acquire_fences.at(1));

  image_pipe_updater_->ScheduleImagePipeUpdate(
      /*presentation_time=*/zx::time(0), image_pipe_->weak_factory_.GetWeakPtr(),
      std::move(acquire_fences), /*release_fences=*/{}, /*callback=*/[](auto...) {});

  RunLoopUntilIdle();
  EXPECT_EQ(schedule_call_count_, 0);

  fence2.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  EXPECT_EQ(schedule_call_count_, 0);

  fence1.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  EXPECT_EQ(schedule_call_count_, 1);
}

TEST_F(ImagePipeUpdaterTest, UpdatesSignaledInOrder_BeforeUpdate_ShouldAllBeScheduled) {
  std::vector<zx::event> acquire_fences1 = CreateEventArray(1);
  zx::event fence1 = CopyEvent(acquire_fences1.at(0));
  std::vector<zx::event> acquire_fences2 = CreateEventArray(1);
  zx::event fence2 = CopyEvent(acquire_fences2.at(0));

  image_pipe_updater_->ScheduleImagePipeUpdate(
      /*presentation_time=*/zx::time(0), image_pipe_->weak_factory_.GetWeakPtr(),
      std::move(acquire_fences1), /*release_fences=*/{}, /*callback=*/[](auto...) {});
  image_pipe_updater_->ScheduleImagePipeUpdate(
      /*presentation_time=*/zx::time(0), image_pipe_->weak_factory_.GetWeakPtr(),
      std::move(acquire_fences2), /*release_fences=*/{}, /*callback=*/[](auto...) {});

  RunLoopUntilIdle();
  EXPECT_EQ(schedule_call_count_, 0);

  fence1.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  EXPECT_EQ(schedule_call_count_, 1);

  fence2.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  EXPECT_EQ(schedule_call_count_, 2);
}

TEST_F(ImagePipeUpdaterTest, UpdatesSignaledOutOfOrder_BeforeUpdate_ShouldStillBeScheduled) {
  std::vector<zx::event> acquire_fences1 = CreateEventArray(1);
  zx::event fence1 = CopyEvent(acquire_fences1.at(0));
  std::vector<zx::event> acquire_fences2 = CreateEventArray(1);
  zx::event fence2 = CopyEvent(acquire_fences2.at(0));

  image_pipe_updater_->ScheduleImagePipeUpdate(
      /*presentation_time=*/zx::time(0), image_pipe_->weak_factory_.GetWeakPtr(),
      std::move(acquire_fences1), /*release_fences=*/{}, /*callback=*/[](auto...) {});
  image_pipe_updater_->ScheduleImagePipeUpdate(
      /*presentation_time=*/zx::time(0), image_pipe_->weak_factory_.GetWeakPtr(),
      std::move(acquire_fences2), /*release_fences=*/{}, /*callback=*/[](auto...) {});

  RunLoopUntilIdle();
  EXPECT_EQ(schedule_call_count_, 0);

  fence2.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  EXPECT_EQ(schedule_call_count_, 1);

  fence1.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  EXPECT_EQ(schedule_call_count_, 2);
}

TEST_F(ImagePipeUpdaterTest, UpdatesSignaledInOrder_AfterUpdate_ShouldBeScheduled) {
  std::vector<zx::event> acquire_fences1 = CreateEventArray(1);
  zx::event fence1 = CopyEvent(acquire_fences1.at(0));
  std::vector<zx::event> acquire_fences2 = CreateEventArray(1);
  zx::event fence2 = CopyEvent(acquire_fences2.at(0));

  scheduling::PresentId present_id1 = image_pipe_updater_->ScheduleImagePipeUpdate(
      /*presentation_time=*/zx::time(0), image_pipe_->weak_factory_.GetWeakPtr(),
      std::move(acquire_fences1), /*release_fences=*/{}, /*callback=*/[](auto...) {});

  scheduling::PresentId present_id2 = image_pipe_updater_->ScheduleImagePipeUpdate(
      /*presentation_time=*/zx::time(0), image_pipe_->weak_factory_.GetWeakPtr(),
      std::move(acquire_fences2), /*release_fences=*/{}, /*callback=*/[](auto...) {});

  RunLoopUntilIdle();
  EXPECT_EQ(schedule_call_count_, 0);

  fence1.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  EXPECT_EQ(schedule_call_count_, 1);

  image_pipe_updater_->UpdateSessions(
      /*sessions_to_update=*/{{image_pipe_updater_->GetSchedulingId(), present_id1}},
      /*trace_id=*/0);

  fence2.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  EXPECT_EQ(schedule_call_count_, 2);
}

TEST_F(ImagePipeUpdaterTest, UpdatesSignaledOutOfOrder_AfterUpdate_ShouldNeverBeScheduled) {
  std::vector<zx::event> acquire_fences1 = CreateEventArray(1);
  zx::event fence1 = CopyEvent(acquire_fences1.at(0));
  std::vector<zx::event> acquire_fences2 = CreateEventArray(1);
  zx::event fence2 = CopyEvent(acquire_fences2.at(0));

  scheduling::PresentId present_id1 = image_pipe_updater_->ScheduleImagePipeUpdate(
      /*presentation_time=*/zx::time(0), image_pipe_->weak_factory_.GetWeakPtr(),
      std::move(acquire_fences1), /*release_fences=*/{}, /*callback=*/[](auto...) {});

  scheduling::PresentId present_id2 = image_pipe_updater_->ScheduleImagePipeUpdate(
      /*presentation_time=*/zx::time(0), image_pipe_->weak_factory_.GetWeakPtr(),
      std::move(acquire_fences2), /*release_fences=*/{}, /*callback=*/[](auto...) {});

  RunLoopUntilIdle();
  EXPECT_EQ(schedule_call_count_, 0);

  fence2.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  EXPECT_EQ(schedule_call_count_, 1);

  image_pipe_updater_->UpdateSessions(
      /*sessions_to_update=*/{{image_pipe_updater_->GetSchedulingId(), present_id2}},
      /*trace_id=*/0);

  fence1.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  EXPECT_EQ(schedule_call_count_, 1);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
