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
    : frame_scheduler_(frame_scheduler) {
  FX_DCHECK(frame_scheduler);
}

ImagePipeUpdater::ImagePipeUpdater() {}

ImagePipeUpdater::~ImagePipeUpdater() {
  if (auto scheduler = frame_scheduler_.lock()) {
    for (const auto& [scheduling_id, _] : image_pipes_) {
      scheduler->RemoveSession(scheduling_id);
    }
  }
}

scheduling::PresentId ImagePipeUpdater::ScheduleImagePipeUpdate(
    scheduling::SessionId scheduling_id, zx::time presentation_time,
    fxl::WeakPtr<ImagePipeBase> image_pipe, std::vector<zx::event> acquire_fences,
    std::vector<zx::event> release_fences,
    fuchsia::images::ImagePipe2::PresentImageCallback callback) {
  TRACE_DURATION("gfx", "ImagePipeUpdater::ScheduleImagePipeUpdate", "scheduling_id",
                 scheduling_id);

  scheduling::PresentId present_id = scheduling::kInvalidPresentId;

  if (image_pipes_.find(scheduling_id) == image_pipes_.end()) {
    image_pipes_[scheduling_id].image_pipe = std::move(image_pipe);
  }

  auto& pipe = image_pipes_.at(scheduling_id);
  if (auto scheduler = frame_scheduler_.lock()) {
    present_id = scheduler->RegisterPresent(scheduling_id, std::move(release_fences));
    const scheduling::SchedulingIdPair id_pair{scheduling_id, present_id};

    pipe.present1_helper.RegisterPresent(present_id, std::move(callback));

    auto [it, success] = fence_listeners_.emplace(id_pair, std::move(acquire_fences));
    FX_DCHECK(success);

    const auto trace_id = SESSION_TRACE_ID(scheduling_id, present_id);
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
            locked_frame_scheduler->ScheduleUpdateForSession(presentation_time, id_pair,
                                                             /*squashable=*/true);
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

  for (const auto& [scheduling_id, present_id] : sessions_to_update) {
    // Destroy all unsignalled acquire fence listeners older than |present_id|.
    RemoveFenceListenersPriorTo(scheduling_id, present_id);

    // Apply update for |present_id|.
    if (image_pipes_.find(scheduling_id) != image_pipes_.end()) {
      if (auto image_pipe = image_pipes_.at(scheduling_id).image_pipe) {
        image_pipe->Update(present_id);
      }
    }
  }

  return results;
}

void ImagePipeUpdater::RemoveFenceListenersPriorTo(scheduling::SessionId scheduling_id,
                                                   scheduling::PresentId present_id) {
  auto begin_it = fence_listeners_.lower_bound({scheduling_id, 0});
  auto end_it = fence_listeners_.upper_bound({scheduling_id, present_id});
  FX_DCHECK(std::distance(begin_it, end_it) >= 0);
  fence_listeners_.erase(begin_it, end_it);
}

void ImagePipeUpdater::OnFramePresented(
    const std::unordered_map<scheduling::SessionId, std::map<scheduling::PresentId, zx::time>>&
        latched_times,
    scheduling::PresentTimestamps present_times) {
  for (const auto& [scheduling_id, latch_map] : latched_times) {
    const auto it = image_pipes_.find(scheduling_id);
    if (it != image_pipes_.end()) {
      it->second.present1_helper.OnPresented(latch_map, present_times);
    }
  }
}

void ImagePipeUpdater::CleanupImagePipe(scheduling::SessionId scheduling_id) {
  const auto it = image_pipes_.find(scheduling_id);
  if (it == image_pipes_.end()) {
    return;
  }

  image_pipes_.erase(it);
  RemoveFenceListenersPriorTo(scheduling_id, std::numeric_limits<scheduling::PresentId>::max());

  // Remove all old updates and schedule a new dummy update to ensure we draw a new clean frame
  // without the removed pipe.
  if (auto scheduler = frame_scheduler_.lock()) {
    scheduler->RemoveSession(scheduling_id);
    const auto present_id = scheduler->RegisterPresent(scheduling_id, /*release_fences*/ {});
    scheduler->ScheduleUpdateForSession(/*presentation_time*/ zx::time(0),
                                        /*id_pair*/ {scheduling_id, present_id},
                                        /*squashable=*/true);
  }
}

}  // namespace gfx
}  // namespace scenic_impl
