// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/render/renderer.h"

#include <lib/async/cpp/task.h>

#include "garnet/bin/mediaplayer/framework/formatting.h"
#include "lib/media/timeline/timeline.h"

namespace media_player {

Renderer::Renderer() { ClearPendingTimelineFunction(); }

Renderer::~Renderer() {}

void Renderer::Provision(async_dispatcher_t* dispatcher,
                         fit::closure update_callback) {
  dispatcher_ = dispatcher;
  update_callback_ = std::move(update_callback);
}

void Renderer::Deprovision() {
  dispatcher_ = nullptr;
  update_callback_ = nullptr;
}

void Renderer::Dump(std::ostream& os) const {
  os << label() << fostr::Indent;
  stage()->Dump(os);
  os << fostr::NewLine
     << "timeline:              " << current_timeline_function();
  os << fostr::NewLine << "end of stream:         " << end_of_stream();
  os << fostr::NewLine
     << "end of stream pts:     " << AsNs(end_of_stream_pts());
  os << fostr::NewLine << "minimum pts:           " << AsNs(program_0_min_pts_);
  os << fostr::NewLine << "maximum pts:           " << AsNs(program_0_max_pts_);
  os << fostr::Outdent;
}

void Renderer::GetConfiguration(size_t* input_count, size_t* output_count) {
  FXL_DCHECK(input_count);
  FXL_DCHECK(output_count);
  *input_count = 1;
  *output_count = 0;
}

void Renderer::SetProgramRange(uint64_t program, int64_t min_pts,
                               int64_t max_pts) {
  FXL_DCHECK(program == 0) << "Only program 0 is currently supported.";
  program_0_min_pts_ = min_pts;
  program_0_max_pts_ = max_pts;
}

void Renderer::SetTimelineFunction(media::TimelineFunction timeline_function,
                                   fit::closure callback) {
  FXL_DCHECK(timeline_function.subject_time() != fuchsia::media::NO_TIMESTAMP);
  FXL_DCHECK(timeline_function.reference_time() !=
             fuchsia::media::NO_TIMESTAMP);
  FXL_DCHECK(timeline_function.reference_delta() != 0);

  bool was_progressing = Progressing();

  // Eject any previous pending change.
  ClearPendingTimelineFunction();

  // Queue up the new pending change.
  pending_timeline_function_ = timeline_function;
  set_timeline_function_callback_ = std::move(callback);

  if (!was_progressing && Progressing()) {
    OnProgressStarted();
  }

  UpdateTimelineAt(timeline_function.reference_time());
}

bool Renderer::end_of_stream() const {
  return end_of_stream_pts_ != fuchsia::media::NO_TIMESTAMP &&
         current_timeline_function_(media::Timeline::local_now()) >=
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
  async::PostTaskForTime(
      dispatcher_, [this, reference_time]() { UpdateTimeline(reference_time); },
      zx::time(reference_time));
}

void Renderer::OnTimelineTransition() {}

void Renderer::ApplyPendingChanges(int64_t reference_time) {
  if (!TimelineFunctionPending() ||
      pending_timeline_function_.reference_time() > reference_time) {
    return;
  }

  current_timeline_function_ = pending_timeline_function_;
  ClearPendingTimelineFunction();
  OnTimelineTransition();
}

void Renderer::ClearPendingTimelineFunction() {
  pending_timeline_function_ = media::TimelineFunction(
      fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP, 0, 1);

  if (set_timeline_function_callback_) {
    fit::closure callback = std::move(set_timeline_function_callback_);
    callback();
  }
}

}  // namespace media_player
