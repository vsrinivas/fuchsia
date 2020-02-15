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
    : scheduling_id_(scheduling::GetNextSessionId()),
      frame_scheduler_(frame_scheduler),
      weak_factory_(this) {
  FXL_DCHECK(frame_scheduler);
  frame_scheduler->AddSessionUpdater(weak_factory_.GetWeakPtr());
  frame_scheduler->SetOnUpdateFailedCallbackForSession(scheduling_id_, [] {
    // ImagePipe updates currently can not fail.
    FXL_CHECK(false);
  });
}

ImagePipeUpdater::ImagePipeUpdater()
    : scheduling_id_(scheduling::GetNextSessionId()), weak_factory_(this){};

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

  scheduling::PresentId present_id = 0;

  // This gets reset to the same value on every frame. Should probably only be set once (per pipe).
  // TODO(45362): Optimize this for either one or several image pipes.
  image_pipes_[scheduling_id_] = std::move(image_pipe);

  if (auto scheduler = frame_scheduler_.lock()) {
    present_id =
        scheduler->RegisterPresent(scheduling_id_, std::move(callback), std::move(release_fences));
    scheduling::SchedulingIdPair id_pair{scheduling_id_, present_id};

    auto [it, success] = fence_listeners_.emplace(id_pair, std::move(acquire_fences));
    FXL_DCHECK(success);

    const auto trace_id = SESSION_TRACE_ID(scheduling_id_, present_id);
    TRACE_FLOW_BEGIN("gfx", "wait_for_fences", trace_id);

    // Set callback for the acquire fence listener.
    auto& fence_listener = it->second;
    fence_listener.WaitReadyAsync(
        [weak = weak_factory_.GetWeakPtr(), id_pair, presentation_time, trace_id]() mutable {
          if (!weak)
            return;
          TRACE_DURATION("gfx", "ImagePipeUpdater::ScheduleImagePipeUpdate::fences_ready");
          TRACE_FLOW_END("gfx", "wait_for_fences", trace_id);

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
    FXL_DCHECK(std::distance(begin_it, end_it) >= 0);
    fence_listeners_.erase(begin_it, end_it);
  }

  // Apply update for |present_id|.
  FXL_DCHECK(image_pipes_.find(session_id) != image_pipes_.end());
  if (auto image_pipe = image_pipes_[session_id]) {
    auto image_pipe_update_results = image_pipe->Update(present_id);
  }

  return results;
}

}  // namespace gfx
}  // namespace scenic_impl
