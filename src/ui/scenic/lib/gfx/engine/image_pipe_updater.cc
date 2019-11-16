// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/image_pipe_updater.h"

#include "src/ui/scenic/lib/gfx/resources/image_pipe_base.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"

namespace scenic_impl {
namespace gfx {

ImagePipeUpdater::ImagePipeUpdater(SessionId id,
                                   std::shared_ptr<scheduling::FrameScheduler> frame_scheduler)
    : session_id_(id), frame_scheduler_(std::move(frame_scheduler)) {}

ImagePipeUpdater::~ImagePipeUpdater() { scheduled_image_pipe_updates_ = {}; }

void ImagePipeUpdater::ScheduleImagePipeUpdate(zx::time presentation_time,
                                               fxl::WeakPtr<ImagePipeBase> image_pipe) {
  // Some tests don't create a frame scheduler, but those should definitely not be triggering
  // ImagePipe updates.
  FXL_DCHECK(frame_scheduler_);

  if (image_pipe) {
    FXL_DCHECK(image_pipe->session_id() == session_id_);
    scheduled_image_pipe_updates_.push({presentation_time, std::move(image_pipe)});
  }
  frame_scheduler_->ScheduleUpdateForSession(presentation_time, session_id_);
}

ImagePipeUpdater::ApplyScheduledUpdatesResult ImagePipeUpdater::ApplyScheduledUpdates(
    zx::time target_presentation_time, escher::ReleaseFenceSignaller* release_fence_signaller) {
  ApplyScheduledUpdatesResult result{.needs_render = false};

  while (!scheduled_image_pipe_updates_.empty() &&
         scheduled_image_pipe_updates_.top().presentation_time <= target_presentation_time) {
    auto& update = scheduled_image_pipe_updates_.top();
    if (update.image_pipe) {
      // NOTE: there is some subtlety in the interaction with ImagePipe::Update().  For various
      // reasons (e.g. dropped frames, Scenic client behavior, etc.) there may be multiple updates
      // scheduled before |target_presentation_time|.  When ImagePipe::Update() is called, the most
      // recent frame before |target_presentation_time| is applied, and any earlier frames are
      // skipped.  Later in this loop, we may encounter another another update for the same
      // ImagePipe, with a later target time, but still <= |target_presentation_time|.  For this
      // reason, ImagePipe::Update() is guaranteed to be idempotent (see the comment on that method
      // for more details).
      auto image_pipe_update_results =
          update.image_pipe->Update(release_fence_signaller, target_presentation_time);

      // Collect callbacks to be returned by |Engine::UpdateSessions()| as part
      // of the |Session::UpdateResults| struct.
      std::move(image_pipe_update_results.callbacks.begin(),
                image_pipe_update_results.callbacks.end(), std::back_inserter(result.callbacks));
      image_pipe_update_results.callbacks.clear();

      // |ImagePipe| needs to be re-rendered if its most recent image is updated.
      result.needs_render = result.needs_render || image_pipe_update_results.image_updated;
    }
    scheduled_image_pipe_updates_.pop();
  }

  return result;
}

}  // namespace gfx
}  // namespace scenic_impl
