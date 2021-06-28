// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/default_flatland_presenter.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

namespace flatland {

DefaultFlatlandPresenter::DefaultFlatlandPresenter(async_dispatcher_t* main_dispatcher)
    : main_dispatcher_(main_dispatcher) {}

void DefaultFlatlandPresenter::SetFrameScheduler(
    const std::shared_ptr<scheduling::FrameScheduler>& frame_scheduler) {
  FX_DCHECK(main_dispatcher_ == async_get_default_dispatcher());
  FX_DCHECK(frame_scheduler_.expired()) << "FrameScheduler already set.";
  frame_scheduler_ = frame_scheduler;
}

scheduling::SessionUpdater::UpdateResults DefaultFlatlandPresenter::UpdateSessions(
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

std::vector<zx::event> DefaultFlatlandPresenter::TakeReleaseFences() {
  FX_DCHECK(main_dispatcher_ == async_get_default_dispatcher());

  std::vector<zx::event> result(std::move(accumulated_release_fences_));
  accumulated_release_fences_.clear();
  return result;
}

scheduling::PresentId DefaultFlatlandPresenter::RegisterPresent(
    scheduling::SessionId session_id, std::vector<zx::event> release_fences) {
  scheduling::PresentId present_id = scheduling::kInvalidPresentId;

  if (auto scheduler = frame_scheduler_.lock()) {
    // Since FrameScheduler::RegisterPresent() will not run immediately, generate a PresentId
    // independently.
    present_id = scheduling::GetNextPresentId();

    // TODO(fxbug.dev/61178): The FrameScheduler is not thread-safe, but a lock is not sufficient
    // since GFX sessions may access the FrameScheduler without passing through this object. Post a
    // task to the main thread, which is where GFX runs, to account for thread safety.
    async::PostTask(main_dispatcher_, [thiz = shared_from_this(), scheduler, present_id, session_id,
                                       release_fences = std::move(release_fences)]() mutable {
      scheduling::SchedulingIdPair id_pair{session_id, present_id};
      FX_DCHECK(thiz->release_fences_.find(id_pair) == thiz->release_fences_.end());
      thiz->release_fences_.emplace(id_pair, std::move(release_fences));

      scheduler->RegisterPresent(session_id, {}, present_id);
    });
  } else {
    // TODO(fxbug.dev/56290): Account for missing FrameScheduler case.
    FX_LOGS(WARNING) << "Cannot register present due to missing FrameScheduler.";
  }

  return present_id;
}

void DefaultFlatlandPresenter::ScheduleUpdateForSession(zx::time requested_presentation_time,
                                                        scheduling::SchedulingIdPair id_pair,
                                                        bool unsquashable) {
  if (auto scheduler = frame_scheduler_.lock()) {
    // TODO(fxbug.dev/61178): The FrameScheduler is not thread-safe, but a lock is not sufficient
    // since GFX sessions may access the FrameScheduler without passing through this object. Post a
    // task to the main thread, which is where GFX runs, to account for thread safety.
    async::PostTask(
        main_dispatcher_, [scheduler, requested_presentation_time, id_pair, unsquashable] {
          scheduler->ScheduleUpdateForSession(requested_presentation_time, id_pair, !unsquashable);
        });
  } else {
    // TODO(fxbug.dev/56290): Account for missing FrameScheduler case.
    FX_LOGS(WARNING) << "Cannot schedule update for session due to missing FrameScheduler.";
  }
}

void DefaultFlatlandPresenter::GetFuturePresentationInfos(
    scheduling::FrameScheduler::GetFuturePresentationInfosCallback presentation_infos_callback) {
  auto flatland_thread_dispatcher = async_get_default_dispatcher();
  FX_DCHECK(flatland_thread_dispatcher);

  if (auto scheduler = frame_scheduler_.lock()) {
    // TODO(fxbug.dev/61178): The FrameScheduler is not thread-safe, but a lock is not sufficient
    // since GFX sessions may access the FrameScheduler without passing through this object. Post a
    // task to the main thread, which is where GFX runs, to account for thread safety.
    async::PostTask(
        main_dispatcher_,
        [flatland_thread_dispatcher, requested_prediction_span = kDefaultPredictionSpan, scheduler,
         presentation_infos_callback = std::move(presentation_infos_callback)]() mutable {
          scheduler->GetFuturePresentationInfos(
              requested_prediction_span,
              [flatland_thread_dispatcher,
               presentation_infos_callback =
                   std::move(presentation_infos_callback)](auto presentation_infos) mutable {
                // Post the frame scheduler's response back on the instance thread for dispatch.
                async::PostTask(
                    flatland_thread_dispatcher,
                    [presentation_infos_callback = std::move(presentation_infos_callback),
                     presentation_infos = std::move(presentation_infos)]() mutable {
                      presentation_infos_callback(std::move(presentation_infos));
                    });
              });
        });
  } else {
    // TODO(fxbug.dev/56290): Account for missing FrameScheduler case.
    FX_LOGS(WARNING) << "Cannot get future presentation infos due to missing FrameScheduler.";
  }
}

void DefaultFlatlandPresenter::RemoveSession(scheduling::SessionId session_id) {
  FX_DCHECK(main_dispatcher_ == async_get_default_dispatcher());

  // Remove any registered release fences for the removed session.
  {
    auto start = release_fences_.lower_bound({session_id, 0});
    auto end = release_fences_.lower_bound({session_id + 1, 0});
    release_fences_.erase(start, end);
  }

  if (auto scheduler = frame_scheduler_.lock()) {
    scheduler->RemoveSession(session_id);
  } else {
    // TODO(fxbug.dev/56290): Account for missing FrameScheduler case.
    FX_LOGS(WARNING) << "Cannot remove session due to missing FrameScheduler.";
  }
}

}  // namespace flatland
