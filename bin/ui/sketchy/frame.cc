// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/frame.h"

#include "garnet/bin/ui/sketchy/buffer/shared_buffer_pool.h"
#include "lib/escher/escher.h"
#include "lib/escher/impl/command_buffer_pool.h"
#include "lib/escher/util/fuchsia_utils.h"

namespace sketchy_service {

Frame::Frame(SharedBufferPool* shared_buffer_pool, bool enable_profiler)
    : shared_buffer_pool_(shared_buffer_pool),
      escher_(shared_buffer_pool->escher()),
      command_(escher_->command_buffer_pool()->GetCommandBuffer()) {
  auto acquire_semaphore_pair = escher::NewSemaphoreEventPair(escher_);
  if (!acquire_semaphore_pair.first) {
    init_failed_ = true;
    return;
  }
  acquire_semaphore_ = std::move(acquire_semaphore_pair.first);
  acquire_fence_ = std::move(acquire_semaphore_pair.second);

  auto status = zx::event::create(/* options= */ 0u, &release_fence_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create release fence.";
    init_failed_ = true;
  }

  if (enable_profiler && escher_->supports_timer_queries()) {
    profiler_ = fxl::MakeRefCounted<escher::TimestampProfiler>(
        escher_->vk_device(), escher_->timestamp_period());
    // Intel/Mesa workaround. See the submit callback underneath.
    profiler_->AddTimestamp(command_, vk::PipelineStageFlagBits::eBottomOfPipe,
                            "Throwaway");
    profiler_->AddTimestamp(command_, vk::PipelineStageFlagBits::eBottomOfPipe,
                            "Start");
  }
}

zx::event Frame::DuplicateReleaseFence() {
  zx::event dup;
  auto result = release_fence_.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
  if (result != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to duplicate event (status: " << result << ").";
  }
  return dup;
}

void Frame::RequestScenicPresent(
    scenic::Session* session, uint64_t presentation_time,
    scenic::Session::PresentCallback callback) {
  if (profiler_) {
    profiler_->AddTimestamp(command_, vk::PipelineStageFlagBits::eBottomOfPipe,
                            "End");
  }
  command_->AddSignalSemaphore(std::move(acquire_semaphore_));
  command_->Submit(
      escher_->device()->vk_main_queue(), [profiler = std::move(profiler_)]() {
        if (!profiler) {
          return;
        }
        FXL_LOG(INFO) << "----------------------------------------------------";
        FXL_LOG(INFO) << "Total (ms)\t | \tSince previous (ms)";
        FXL_LOG(INFO) << "----------------------------------------------------";
        auto timestamps = profiler->GetQueryResults();

        // Workaround: Intel/Mesa gives a screwed-up value for the second time.
        // So, we add a throwaway value first (see BeginFrame()), and then fix
        // up the first value that we actually print.
        timestamps[1].time = 0;
        timestamps[1].elapsed = 0;
        timestamps[2].elapsed = timestamps[2].time;

        for (size_t i = 1; i < timestamps.size(); ++i) {
          FXL_LOG(INFO) << timestamps[i].time * 1e-3 << "\t | \t"
                        << timestamps[i].elapsed * 1e-3 << "   \t"
                        << timestamps[i].name;
        }
        FXL_LOG(INFO) << "----------------------------------------------------";
      });
  session->EnqueueAcquireFence(std::move(acquire_fence_));
  session->EnqueueReleaseFence(std::move(release_fence_));
  session->Present(presentation_time, std::move(callback));
}

}  // namespace sketchy_service
