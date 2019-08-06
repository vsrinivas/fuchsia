// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/image_pipe_updater.h"

#include "garnet/lib/ui/gfx/engine/frame_scheduler.h"
#include "garnet/lib/ui/gfx/resources/image_pipe.h"
#include "garnet/lib/ui/gfx/util/collection_utils.h"

namespace scenic_impl {
namespace gfx {

ImagePipeUpdater::ImagePipeUpdater(SessionId id, std::shared_ptr<FrameScheduler> frame_scheduler)
    : session_id_(id), frame_scheduler_(std::move(frame_scheduler)) {}

ImagePipeUpdater::~ImagePipeUpdater() { scheduled_image_pipe_updates_ = {}; }

void ImagePipeUpdater::ScheduleImagePipeUpdate(zx::time presentation_time,
                                               const ImagePipePtr& image_pipe) {
  // Some tests don't create a frame scheduler, but those should definitely not be triggering
  // ImagePipe updates.
  FXL_DCHECK(frame_scheduler_);

  if (image_pipe) {
    FXL_DCHECK(image_pipe->session_id() == session_id_);
    scheduled_image_pipe_updates_.push({presentation_time, image_pipe->GetWeakPtr()});
  }
  frame_scheduler_->ScheduleUpdateForSession(presentation_time, session_id_);
}

ImagePipeUpdater::ApplyScheduledUpdatesResult ImagePipeUpdater::ApplyScheduledUpdates(
    CommandContext* command_context, zx::time target_presentation_time,
    escher::ReleaseFenceSignaller* release_fence_signaller) {
  ApplyScheduledUpdatesResult result{.needs_render = false};

  std::unordered_map<ResourceId, ImagePipePtr> image_pipe_updates_to_upload;
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
      MoveAllItemsFromQueueToQueue(&image_pipe_update_results.callbacks, &result.callbacks);

      // Only upload images that were updated and are currently dirty, and only
      // do one upload per ImagePipe.
      if (image_pipe_update_results.image_updated) {
        image_pipe_updates_to_upload.try_emplace(update.image_pipe->id(),
                                                 ImagePipePtr(update.image_pipe.get()));
      }
    }
    scheduled_image_pipe_updates_.pop();
  }

  // Stage GPU uploads for the latest dirty image on each updated ImagePipe.
  for (const auto& entry : image_pipe_updates_to_upload) {
    ImagePipePtr image_pipe = entry.second;
    image_pipe->UpdateEscherImage(command_context->batch_gpu_uploader());
    // Image was updated so the image in the scene is dirty.
    result.needs_render = true;
  }
  image_pipe_updates_to_upload.clear();

  return result;
}

}  // namespace gfx
}  // namespace scenic_impl
