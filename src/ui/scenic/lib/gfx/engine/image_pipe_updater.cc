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
  FXL_DCHECK(frame_scheduler);
  frame_scheduler->AddSessionUpdater(weak_factory_.GetWeakPtr());
  frame_scheduler->SetOnUpdateFailedCallbackForSession(
      scheduling_id_, [scheduling_id = scheduling_id_, frame_scheduler = frame_scheduler_] {
        if (auto scheduler = frame_scheduler.lock()) {
          scheduler->ClearCallbacksForSession(scheduling_id);
        }
      });
}

ImagePipeUpdater::ImagePipeUpdater()
    : scheduling_id_(scheduling::GetNextSessionId()), weak_factory_(this){};

ImagePipeUpdater::~ImagePipeUpdater() {
  if (auto scheduler = frame_scheduler_.lock()) {
    scheduler->ClearCallbacksForSession(scheduling_id_);
  }
}

scheduling::PresentId ImagePipeUpdater::ScheduleImagePipeUpdate(
    zx::time presentation_time, fxl::WeakPtr<ImagePipeBase> image_pipe,
    std::vector<zx::event> acquire_fences, std::vector<zx::event> release_fences,
    fuchsia::images::ImagePipe::PresentImageCallback callback) {
  TRACE_DURATION("gfx", "ImagePipeUpdater::ScheduleImagePipeUpdate", "scheduling_id",
                 scheduling_id_);

  const scheduling::PresentId present_id = scheduling::GetNextPresentId();
  scheduling::SchedulingIdPair id_pair{scheduling_id_, present_id};

  // This gets reset to the same value on every frame. Should probably only be set once (per pipe).
  // TODO(45362): Optimize this for either one or several image pipes.
  image_pipes_[scheduling_id_] = std::move(image_pipe);
  release_fences_.emplace(id_pair, std::move(release_fences));
  callbacks_.emplace(id_pair, std::move(callback));

  if (auto scheduler = frame_scheduler_.lock()) {
    auto [it, success] = fence_listeners_.emplace(id_pair, std::move(acquire_fences));
    FXL_DCHECK(success);

    // Set callback for the acquire fence listener.
    auto& fence_listener = it->second;
    fence_listener.WaitReadyAsync(
        [weak = weak_factory_.GetWeakPtr(), id_pair, presentation_time]() mutable {
          if (!weak)
            return;

          if (auto locked_frame_scheduler = weak->frame_scheduler_.lock()) {
            locked_frame_scheduler->ScheduleUpdateForSession(presentation_time, id_pair);
          }

          // Release fences have been moved into frame scheduler. Delete the remaining fence
          // listener.
          weak->fence_listeners_.erase(id_pair);
        });
  }

  return present_id;
}

ImagePipeUpdater::UpdateResults ImagePipeUpdater::UpdateSessions(
    const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
    zx::time presentation_time, zx::time latched_time, uint64_t trace_id) {
  UpdateResults results{.needs_render = false};
  if (sessions_to_update.find(scheduling_id_) == sessions_to_update.end()) {
    return results;
  }

  const scheduling::SessionId session_id = scheduling_id_;
  const scheduling::PresentId present_id = sessions_to_update.at(session_id);

  // Destroy all unsignalled acquire fence listeners older than |present_id|.
  {
    auto begin_it = fence_listeners_.lower_bound({session_id, 0});
    auto end_it = fence_listeners_.upper_bound({session_id, present_id});
    FXL_DCHECK(std::distance(begin_it, end_it) >= 0);
    fence_listeners_.erase(begin_it, end_it);
  }

  {
    // Move release fences prior to |present_id| to the signaller to be signaled when GPU work is
    // done. |release_fences_| contains all release fences, including the ones for whatever image is
    // currently presented, skipped images and scheduled future images.
    auto begin_it = release_fences_.lower_bound({session_id, 0});
    // Safe because |present_id| always > 0.
    auto end_it = release_fences_.upper_bound({session_id, present_id - 1});
    FXL_DCHECK(std::distance(begin_it, end_it) >= 0);
    if (release_fence_signaller_) {
      for (auto it = begin_it; it != end_it; ++it) {
        release_fence_signaller_->AddCPUReleaseFences(std::move(it->second));
      }
    }
    release_fences_.erase(begin_it, end_it);
  }

  {
    // Move callbacks up to and including |present_id| to be triggered when this update is
    // presented.
    auto begin_it = callbacks_.lower_bound({session_id, 0});
    auto end_it = callbacks_.upper_bound({session_id, present_id});
    FXL_DCHECK(std::distance(begin_it, end_it) >= 0);
    for (auto it = begin_it; it != end_it; ++it) {
      results.present1_callbacks.emplace_front(it->first.session_id, std::move(it->second));
    }
    callbacks_.erase(begin_it, end_it);
  }

  // Apply update for |present_id|.
  FXL_DCHECK(image_pipes_.find(session_id) != image_pipes_.end());
  if (auto image_pipe = image_pipes_[session_id]) {
    auto image_pipe_update_results = image_pipe->Update(present_id);
    results.needs_render = true;

    // TODO(43328): This is a copy of the logic in gfx_system, because only SessionUpdaters know
    // which sessions, and how many present calls, have actually been processed. Once this logic has
    // been moved into frame scheduler directly, this logic can be removed.
    TRACE_FLOW_BEGIN("gfx", "needs_render", needs_render_count_);
    ++needs_render_count_;
  }

  return results;
}

void ImagePipeUpdater::PrepareFrame(zx::time target_presentation_time, uint64_t trace_id) {
  while (processed_needs_render_count_ < needs_render_count_) {
    TRACE_FLOW_END("gfx", "needs_render", processed_needs_render_count_);
    ++processed_needs_render_count_;
  }
}

}  // namespace gfx
}  // namespace scenic_impl
