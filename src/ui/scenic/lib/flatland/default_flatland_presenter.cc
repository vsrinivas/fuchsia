#include "src/ui/scenic/lib/flatland/default_flatland_presenter.h"

#include <lib/async/cpp/task.h>

namespace flatland {

DefaultFlatlandPresenter::DefaultFlatlandPresenter(async_dispatcher_t* main_dispatcher)
    : main_dispatcher_(main_dispatcher) {}

void DefaultFlatlandPresenter::SetFrameScheduler(
    const std::shared_ptr<scheduling::FrameScheduler>& frame_scheduler) {
  FX_DCHECK(frame_scheduler_.expired()) << "FrameScheduler already set.";
  frame_scheduler_ = frame_scheduler;
}

scheduling::PresentId DefaultFlatlandPresenter::RegisterPresent(
    scheduling::SessionId session_id, std::vector<zx::event> release_fences) {
  scheduling::PresentId present_id = scheduling::kInvalidPresentId;

  // TODO(fxbug.dev/56290): Account for missing FrameScheduler case.
  if (auto scheduler = frame_scheduler_.lock()) {
    // Since FrameScheduler::RegisterPresent() will not run immediately, generate a PresentId
    // independently.
    present_id = scheduling::GetNextPresentId();

    // TODO(fxbug.dev/61178): The FrameScheduler is not thread-safe, but a lock is not sufficient
    // since GFX sessions may access the FrameScheduler without passing through this object. Post a
    // task to the main thread, which is where GFX runs, to account for thread safety.
    async::PostTask(main_dispatcher_, [scheduler, present_id, session_id,
                                       release_fences = std::move(release_fences)]() mutable {
      scheduler->RegisterPresent(
          session_id, /*present_information=*/[](auto...) {}, std::move(release_fences),
          present_id);
    });
  }

  return present_id;
}

void DefaultFlatlandPresenter::ScheduleUpdateForSession(zx::time requested_presentation_time,
                                                        scheduling::SchedulingIdPair id_pair) {
  // TODO(fxbug.dev/56290): Account for missing FrameScheduler case.
  if (auto scheduler = frame_scheduler_.lock()) {
    // TODO(fxbug.dev/61178): The FrameScheduler is not thread-safe, but a lock is not sufficient
    // since GFX sessions may access the FrameScheduler without passing through this object. Post a
    // task to the main thread, which is where GFX runs, to account for thread safety.
    async::PostTask(main_dispatcher_, [scheduler, requested_presentation_time, id_pair] {
      scheduler->ScheduleUpdateForSession(requested_presentation_time, id_pair);
    });
  }
}

}  // namespace flatland
