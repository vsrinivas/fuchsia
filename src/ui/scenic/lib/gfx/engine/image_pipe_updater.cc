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
    const std::shared_ptr<scheduling::FrameScheduler>& frame_scheduler)
    : scheduling_id_(scheduling::GetNextSessionId()), frame_scheduler_(frame_scheduler) {
  FX_DCHECK(frame_scheduler);
}

ImagePipeUpdater::ImagePipeUpdater() : scheduling_id_(scheduling::GetNextSessionId()) {}

ImagePipeUpdater::~ImagePipeUpdater() {
  if (auto scheduler = frame_scheduler_.lock()) {
    scheduler->RemoveSession(scheduling_id_);
  }
}

scheduling::PresentId ImagePipeUpdater::ScheduleImagePipeUpdate(
    zx::time presentation_time, fxl::WeakPtr<ImagePipeBase> image_pipe,
    std::vector<zx::event> acquire_fences, std::vector<zx::event> release_fences,
    fuchsia::images::ImagePipe::PresentImageCallback callback) {
  TRACE_DURATION("gfx", "ImagePipeUpdater::ScheduleImagePipeUpdate", "scheduling_id",
                 scheduling_id_);

  scheduling::PresentId present_id = scheduling::kInvalidPresentId;

  // This gets reset to the same value on every frame. Should probably only be set once (per pipe).
  // TODO(fxbug.dev/45362): Optimize this for either one or several image pipes.
  image_pipes_[scheduling_id_] = std::move(image_pipe);

  if (auto scheduler = frame_scheduler_.lock()) {
    // TODO(fxbug.dev/47308): Delete callback argument from signature entirely.
    present_id = scheduler->RegisterPresent(
        scheduling_id_, /*callback*/ [](auto...) {}, std::move(release_fences));
    scheduling::SchedulingIdPair id_pair{scheduling_id_, present_id};

    present1_helper_.RegisterPresent(present_id, std::move(callback));

    auto [it, success] = fence_listeners_.emplace(id_pair, std::move(acquire_fences));
    FX_DCHECK(success);

    const auto trace_id = SESSION_TRACE_ID(scheduling_id_, present_id);
    TRACE_FLOW_BEGIN("gfx", "wait_for_fences", trace_id);

    // Set callback for the acquire fence listener.
    auto& fence_listener = it->second;
    fence_listener.WaitReadyAsync(
        [weak = weak_from_this(), id_pair, presentation_time, trace_id]() mutable {
          auto this_locked = weak.lock();
          if (!this_locked)
            return;
          TRACE_DURATION("gfx", "ImagePipeUpdater::ScheduleImagePipeUpdate::fences_ready");
          TRACE_FLOW_END("gfx", "wait_for_fences", trace_id);

          if (auto locked_frame_scheduler = this_locked->frame_scheduler_.lock()) {
            locked_frame_scheduler->ScheduleUpdateForSession(presentation_time, id_pair);
          }

          // Release fences have been moved into frame scheduler. Delete the remaining fence
          // listener.
          this_locked->fence_listeners_.erase(id_pair);
        });
  }

  return present_id;
}

ImagePipeUpdater::UpdateResults ImagePipeUpdater::UpdateSessions(
    const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
    uint64_t trace_id) {
  UpdateResults results{};
  if (sessions_to_update.find(scheduling_id_) == sessions_to_update.end()) {
    return results;
  }

  const scheduling::SessionId session_id = scheduling_id_;
  const scheduling::PresentId present_id = sessions_to_update.at(session_id);

  // Destroy all unsignalled acquire fence listeners older than |present_id|.
  {
    auto begin_it = fence_listeners_.lower_bound({session_id, 0});
    auto end_it = fence_listeners_.upper_bound({session_id, present_id});
    FX_DCHECK(std::distance(begin_it, end_it) >= 0);
    fence_listeners_.erase(begin_it, end_it);
  }

  // Apply update for |present_id|.
  FX_DCHECK(image_pipes_.find(session_id) != image_pipes_.end());
  if (auto image_pipe = image_pipes_[session_id]) {
    auto image_pipe_update_results = image_pipe->Update(present_id);
  }

  return results;
}

void ImagePipeUpdater::OnFramePresented(
    const std::unordered_map<scheduling::SessionId, std::map<scheduling::PresentId, zx::time>>&
        latched_times,
    scheduling::PresentTimestamps present_times) {
  const auto it = latched_times.find(scheduling_id_);
  if (it != latched_times.end()) {
    present1_helper_.OnPresented(/*latched_times*/ it->second, present_times);
  }
}

}  // namespace gfx
}  // namespace scenic_impl
