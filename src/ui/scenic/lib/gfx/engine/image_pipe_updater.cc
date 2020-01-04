// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/image_pipe_updater.h"

#include "lib/trace/internal/event_common.h"
#include "src/ui/scenic/lib/gfx/resources/image_pipe_base.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"

namespace scenic_impl {
namespace gfx {

ImagePipeUpdater::ImagePipeUpdater(
    const std::shared_ptr<scheduling::FrameScheduler>& frame_scheduler,
    escher::ReleaseFenceSignaller* release_fence_signaller)
    : scheduling_id_(scheduling::GetNextSessionId()),
      frame_scheduler_(frame_scheduler),
      release_fence_signaller_(release_fence_signaller),
      weak_factory_(this) {
  frame_scheduler->AddSessionUpdater(weak_factory_.GetWeakPtr());
}

void ImagePipeUpdater::ScheduleImagePipeUpdate(zx::time presentation_time,
                                               fxl::WeakPtr<ImagePipeBase> image_pipe) {
  scheduled_image_pipe_updates_.push({presentation_time, std::move(image_pipe)});
  if (auto locked_frame_scheduler = frame_scheduler_.lock()) {
    locked_frame_scheduler->ScheduleUpdateForSession(presentation_time, scheduling_id_);
  }
}

ImagePipeUpdater::UpdateResults ImagePipeUpdater::UpdateSessions(
    const std::unordered_set<scheduling::SessionId>& sessions_to_update, zx::time presentation_time,
    zx::time latched_time, uint64_t trace_id) {
  UpdateResults result{.needs_render = false};

  if (sessions_to_update.count(scheduling_id_) == 0)
    return result;

  while (!scheduled_image_pipe_updates_.empty() &&
         scheduled_image_pipe_updates_.top().presentation_time <= presentation_time) {
    auto& update = scheduled_image_pipe_updates_.top();
    if (update.image_pipe) {
      // NOTE: there is some subtlety in the interaction with ImagePipe::Update().  For various
      // reasons (e.g. dropped frames, Scenic client behavior, etc.) there may be multiple
      // updates scheduled before |presentation_time|.  When ImagePipe::Update() is called, the
      // most recent frame before |presentation_time| is applied, and any earlier frames are
      // skipped.  Later in this loop, we may encounter another another update for the same
      // ImagePipe, with a later target time, but still <= |presentation_time|.  For this
      // reason, ImagePipe::Update() is guaranteed to be idempotent (see the comment on that
      // method for more details).
      FXL_DCHECK(release_fence_signaller_);
      auto image_pipe_update_results =
          update.image_pipe->Update(release_fence_signaller_, presentation_time);

      // Collect callbacks to be returned by |Engine::UpdateSessions()| as part
      // of the |Session::UpdateResults| struct.
      auto itr = image_pipe_update_results.callbacks.begin();
      while (itr != image_pipe_update_results.callbacks.end()) {
        result.present1_callbacks.push_back({scheduling_id_, std::move(*itr)});
        ++itr;
      }
      image_pipe_update_results.callbacks.clear();

      // |ImagePipe| needs to be re-rendered if its most recent image is updated.
      result.needs_render = result.needs_render || image_pipe_update_results.image_updated;
    }
    scheduled_image_pipe_updates_.pop();
  }

  // TODO(43328): This is a copy of the logic in gfx_system, because only SessionUpdaters know which
  // sessions, and how many present calls, have actually been processed. Once this logic has been
  // moved into frame scheduler directly, this logic can be removed.
  if (result.needs_render) {
    TRACE_FLOW_BEGIN("gfx", "needs_render", needs_render_count_);
    ++needs_render_count_;
  }

  return result;
}

void ImagePipeUpdater::PrepareFrame(zx::time target_presentation_time, uint64_t trace_id) {
  while (processed_needs_render_count_ < needs_render_count_) {
    TRACE_FLOW_END("gfx", "needs_render", processed_needs_render_count_);
    ++processed_needs_render_count_;
  }
}

}  // namespace gfx
}  // namespace scenic_impl
