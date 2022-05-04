// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_HELPERS_PRESENTATION_TIMELINE_H_
#define SRC_MEDIA_VNEXT_LIB_HELPERS_PRESENTATION_TIMELINE_H_

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include "src/media/vnext/lib/helpers/scheduled_presentation_time.h"

namespace fmlib {

// Smarter version of |fuchsia::media2::PresentationTimeline|.
class PresentationTimeline {
 public:
  // Constructs a default |PresentationTimeline| with zero correlated times, a rate of 1 and not
  // progressing.
  PresentationTimeline() = default;

  // Constructs a |PresentationTimeline|.
  explicit PresentationTimeline(zx::duration presentation_time, zx::time reference_time,
                                float rate = 1.0f, bool progressing = false);

  // Constructs a |PresentationTimeline|.
  explicit PresentationTimeline(ScheduledPresentationTime time, float rate = 1.0f,
                                bool progressing = false);

  // Constructs a |PresentationTimeline|.
  explicit PresentationTimeline(fuchsia::media2::PresentationTimeline timeline);

  // Returns an equivalent |fuchsia::media2::PresentationTimeline|.
  fuchsia::media2::PresentationTimeline fidl() const;

  // Returns an equivalent |fuchsia::media2::PresentationTimeline|.
  operator fuchsia::media2::PresentationTimeline() const;

  // Returns the combined presentation and reference times.
  ScheduledPresentationTime time() const { return time_; }

  // Returns the presentation time that correlates to |reference_time()|.
  zx::duration initial_presentation_time() const { return time_.presentation_time(); }

  // Returns the reference time that correlates to |presentation_time()|.
  zx::time initial_reference_time() const { return time_.reference_time(); }

  // Returns the rate.
  float rate() const { return rate_; }

  // Returns an indication of whether the presentation timeline is currently progressing.
  bool progressing() const { return progressing_; }

  // Returns a mutable reference to the combined presentation and reference times.
  ScheduledPresentationTime& time() { return time_; }

  // Returns a mutable reference to the presentation time that correlates to |reference_time()|.
  zx::duration& initial_presentation_time() { return time_.presentation_time(); }

  // Returns a mutable reference to the reference time that correlates to |presentation_time()|.
  zx::time& initial_reference_time() { return time_.reference_time(); }

  // Returns a mutable reference to the rate.
  float& rate() { return rate_; }

  // Returns a mutable reference to an indication of whether the presentation timeline is currently
  // progressing.
  bool& progressing() { return progressing_; }

  // Converts a reference time to a presentation time. If the timeline is not progressing, returns
  // the same value as |presentation_time()|.
  zx::duration ToPresentationTime(zx::time reference_time) const;

  // Converts a presentation time to a reference time. The calculation is performed as if the
  // timeline were progressing, whethere it is or not.
  zx::time ToReferenceTime(zx::duration presentation_time) const;

 private:
  ScheduledPresentationTime time_;
  float rate_ = 1.0f;
  bool progressing_ = false;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_HELPERS_PRESENTATION_TIMELINE_H_
