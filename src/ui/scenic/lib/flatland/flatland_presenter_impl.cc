// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland_presenter_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace flatland {

FlatlandPresenterImpl::FlatlandPresenterImpl(async_dispatcher_t* main_dispatcher,
                                             scheduling::FrameScheduler& frame_scheduler)
    : main_dispatcher_(main_dispatcher), frame_scheduler_(frame_scheduler) {}

scheduling::SessionUpdater::UpdateResults FlatlandPresenterImpl::UpdateSessions(
    const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
    uint64_t trace_id) {
  FX_DCHECK(main_dispatcher_ == async_get_default_dispatcher());

  for (const auto& [session_id, present_id] : sessions_to_update) {
    const auto begin_it = release_fences_.lower_bound({session_id, 0});
    const auto end_it = release_fences_.upper_bound({session_id, present_id});
    FX_DCHECK(std::distance(begin_it, end_it) >= 0);
    std::for_each(
        begin_it, end_it,
        [this](
            std::pair<const scheduling::SchedulingIdPair, std::vector<zx::event>>& release_fences) {
          std::move(std::begin(release_fences.second), std::end(release_fences.second),
                    std::back_inserter(accumulated_release_fences_));
        });
    release_fences_.erase(begin_it, end_it);
  }

  // There is no way for any updates to fail, since the code above is simply gathering a vector of
  // fences; it has no visibility into changes to the scene graph.
  return UpdateResults{};
}

std::vector<zx::event> FlatlandPresenterImpl::TakeReleaseFences() {
  FX_DCHECK(main_dispatcher_ == async_get_default_dispatcher());

  std::vector<zx::event> result(std::move(accumulated_release_fences_));
  accumulated_release_fences_.clear();
  return result;
}

void FlatlandPresenterImpl::ScheduleUpdateForSession(zx::time requested_presentation_time,
                                                     scheduling::SchedulingIdPair id_pair,
                                                     bool unsquashable,
                                                     std::vector<zx::event> release_fences) {
  // TODO(fxbug.dev/61178): The FrameScheduler is not thread-safe, but a lock is not sufficient
  // since GFX sessions may access the FrameScheduler without passing through this object. Post a
  // task to the main thread, which is where GFX runs, to account for thread safety.
  async::PostTask(
      main_dispatcher_, [thiz = shared_from_this(), requested_presentation_time, id_pair,
                         unsquashable, release_fences = std::move(release_fences)]() mutable {
        TRACE_DURATION("gfx", "FlatlandPresenterImpl::ScheduleUpdateForSession[task]");
        FX_DCHECK(thiz->release_fences_.find(id_pair) == thiz->release_fences_.end());
        thiz->release_fences_.emplace(id_pair, std::move(release_fences));
        thiz->frame_scheduler_.RegisterPresent(id_pair.session_id, {}, id_pair.present_id);
        thiz->frame_scheduler_.ScheduleUpdateForSession(requested_presentation_time, id_pair,
                                                        !unsquashable);
      });
}

std::vector<scheduling::FuturePresentationInfo>
FlatlandPresenterImpl::GetFuturePresentationInfos() {
  FX_DCHECK(main_dispatcher_ == async_get_default_dispatcher());
  return frame_scheduler_.GetFuturePresentationInfos(kDefaultPredictionSpan);
}

void FlatlandPresenterImpl::RemoveSession(scheduling::SessionId session_id) {
  FX_DCHECK(main_dispatcher_ == async_get_default_dispatcher());

  // Remove any registered release fences for the removed session.
  {
    auto start = release_fences_.lower_bound({session_id, 0});
    auto end = release_fences_.lower_bound({session_id + 1, 0});
    release_fences_.erase(start, end);
  }

  frame_scheduler_.RemoveSession(session_id);
}

}  // namespace flatland
