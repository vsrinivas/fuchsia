// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_HELPERS_SCHEDULED_PRESENTATION_TIME_H_
#define SRC_MEDIA_VNEXT_LIB_HELPERS_SCHEDULED_PRESENTATION_TIME_H_

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

namespace fmlib {

// A presentation time with a correlated reference time.
class ScheduledPresentationTime {
 public:
  ScheduledPresentationTime() = default;

  ScheduledPresentationTime(zx::duration presentation_time, zx::time reference_time);

  zx::duration presentation_time() const { return presentation_time_; }

  zx::time reference_time() const { return reference_time_; }

  zx::duration& presentation_time() { return presentation_time_; }

  zx::time& reference_time() { return reference_time_; }

  ScheduledPresentationTime operator+(zx::duration addend);

  ScheduledPresentationTime operator-(zx::duration subtrahend);

  ScheduledPresentationTime& operator+=(zx::duration addend);

  ScheduledPresentationTime& operator-=(zx::duration subtrahend);

  // Converts a reference time to a presentation time based in a give rate.
  zx::duration ToPresentationTime(zx::time reference_time, float rate = 1.0f) const;

  // Converts a presentation time to a reference time based in a give rate.
  zx::time ToReferenceTime(zx::duration presentation_time, float rate = 1.0f) const;

 private:
  zx::duration presentation_time_;
  zx::time reference_time_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_HELPERS_SCHEDULED_PRESENTATION_TIME_H_
