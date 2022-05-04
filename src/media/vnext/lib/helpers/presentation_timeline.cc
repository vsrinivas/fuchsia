// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/helpers/presentation_timeline.h"

namespace fmlib {

PresentationTimeline::PresentationTimeline(zx::duration presentation_time, zx::time reference_time,
                                           float rate, bool progressing)
    : time_(ScheduledPresentationTime(presentation_time, reference_time)),
      rate_(rate),
      progressing_(progressing) {}

PresentationTimeline::PresentationTimeline(ScheduledPresentationTime time, float rate,
                                           bool progressing)
    : time_(time), rate_(rate), progressing_(progressing) {}

PresentationTimeline::PresentationTimeline(fuchsia::media2::PresentationTimeline timeline)
    : time_(ScheduledPresentationTime(zx::duration(timeline.initial_presentation_time),
                                      zx::time(timeline.initial_reference_time))),
      rate_(timeline.rate),
      progressing_(timeline.progressing) {}

fuchsia::media2::PresentationTimeline PresentationTimeline::fidl() const {
  return fuchsia::media2::PresentationTimeline{
      .initial_presentation_time = time_.presentation_time().get(),
      .initial_reference_time = time_.reference_time().get(),
      .rate = rate_,
      .progressing = progressing_};
}

PresentationTimeline::operator fuchsia::media2::PresentationTimeline() const {
  return fuchsia::media2::PresentationTimeline{
      .initial_presentation_time = time_.presentation_time().get(),
      .initial_reference_time = time_.reference_time().get(),
      .rate = rate_,
      .progressing = progressing_};
}

zx::duration PresentationTimeline::ToPresentationTime(zx::time reference_time) const {
  if (!progressing_) {
    // If we're not progressing, |time_.presentation_time()| is the presentation time at which we
    // are stopped. Return that.
    return time_.presentation_time();
  }

  return time_.ToPresentationTime(reference_time, rate_);
}

zx::time PresentationTimeline::ToReferenceTime(zx::duration presentation_time) const {
  return time_.ToReferenceTime(presentation_time, rate_);
}

}  // namespace fmlib
