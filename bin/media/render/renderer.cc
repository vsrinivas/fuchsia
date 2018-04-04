// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/render/renderer.h"

#include "lib/media/timeline/timeline.h"

namespace media {

Renderer::Renderer() {
  ClearPendingTimelineFunction();
}

Renderer::~Renderer() {}

void Renderer::Provision(fxl::RefPtr<fxl::TaskRunner> task_runner,
                         fxl::Closure update_callback) {
  task_runner_ = task_runner;
  update_callback_ = update_callback;
}

void Renderer::Deprovision() {
  task_runner_ = nullptr;
  update_callback_ = nullptr;
}

void Renderer::SetProgramRange(uint64_t program,
                               int64_t min_pts,
                               int64_t max_pts) {
  FXL_DCHECK(program == 0) << "Only program 0 is currently supported.";
  program_0_min_pts_ = min_pts;
  program_0_max_pts_ = max_pts;
}

void Renderer::SetTimelineFunction(TimelineFunction timeline_function,
                                   fxl::Closure callback) {
  FXL_DCHECK(timeline_function.subject_time() != kUnspecifiedTime);
  FXL_DCHECK(timeline_function.reference_time() != kUnspecifiedTime);
  FXL_DCHECK(timeline_function.reference_delta() != 0);

  bool was_progressing = Progressing();

  // Eject any previous pending change.
  ClearPendingTimelineFunction();

  // Queue up the new pending change.
  pending_timeline_function_ = timeline_function;
  set_timeline_function_callback_ = callback;

  if (!was_progressing && Progressing()) {
    OnProgressStarted();
  }
}

bool Renderer::end_of_stream() const {
  return end_of_stream_pts_ != kUnspecifiedTime &&
         current_timeline_function_(Timeline::local_now()) >=
             end_of_stream_pts_;
}

void Renderer::NotifyUpdate() {
  if (update_callback_) {
    update_callback_();
  }
}

bool Renderer::Progressing() {
  return !end_of_stream_published_ &&
         (current_timeline_function_.subject_delta() != 0 ||
          pending_timeline_function_.subject_delta() != 0);
}

void Renderer::SetEndOfStreamPts(int64_t end_of_stream_pts) {
  if (end_of_stream_pts_ != end_of_stream_pts) {
    end_of_stream_pts_ = end_of_stream_pts;
    end_of_stream_published_ = false;
  }
}

void Renderer::UpdateTimeline(int64_t reference_time) {
  ApplyPendingChanges(reference_time);

  if (end_of_stream() && !end_of_stream_published_) {
    end_of_stream_published_ = true;
    NotifyUpdate();
  }
}

void Renderer::UpdateTimelineAt(int64_t reference_time) {
  task_runner()->PostTaskForTime(
      [this, reference_time]() { UpdateTimeline(reference_time); },
      fxl::TimePoint::FromEpochDelta(
          fxl::TimeDelta::FromNanoseconds(reference_time)));
}

void Renderer::ApplyPendingChanges(int64_t reference_time) {
  if (!TimelineFunctionPending() ||
      pending_timeline_function_.reference_time() > reference_time) {
    return;
  }

  current_timeline_function_ = pending_timeline_function_;
  ClearPendingTimelineFunction();
}

void Renderer::ClearPendingTimelineFunction() {
  pending_timeline_function_ =
      TimelineFunction(kUnspecifiedTime, kUnspecifiedTime, 0, 1);

  if (set_timeline_function_callback_) {
    fxl::Closure callback = set_timeline_function_callback_;
    set_timeline_function_callback_ = nullptr;
    callback();
  }
}

}  // namespace media
