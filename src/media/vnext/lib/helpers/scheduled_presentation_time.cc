// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/helpers/scheduled_presentation_time.h"

namespace fmlib {

ScheduledPresentationTime::ScheduledPresentationTime(zx::duration presentation_time,
                                                     zx::time reference_time)
    : presentation_time_(presentation_time), reference_time_(reference_time) {}

ScheduledPresentationTime ScheduledPresentationTime::operator+(zx::duration addend) {
  return ScheduledPresentationTime(presentation_time_ + addend, reference_time_ + addend);
}

ScheduledPresentationTime ScheduledPresentationTime::operator-(zx::duration subtrahend) {
  return ScheduledPresentationTime(presentation_time_ - subtrahend, reference_time_ - subtrahend);
}

ScheduledPresentationTime& ScheduledPresentationTime::operator+=(zx::duration addend) {
  presentation_time_ += addend;
  reference_time_ += addend;
  return *this;
}

ScheduledPresentationTime& ScheduledPresentationTime::operator-=(zx::duration subtrahend) {
  presentation_time_ -= subtrahend;
  reference_time_ -= subtrahend;
  return *this;
}

zx::duration ScheduledPresentationTime::ToPresentationTime(zx::time reference_time,
                                                           float rate) const {
  // TODO(dalesat): rate?
  FX_CHECK(rate == 1.0f) << "rates other than 1.0 not supported";
  return presentation_time_ + (reference_time - reference_time_);
}

zx::time ScheduledPresentationTime::ToReferenceTime(zx::duration presentation_time,
                                                    float rate) const {
  // TODO(dalesat): rate?
  FX_CHECK(rate == 1.0f) << "rates other than 1.0 not supported";
  return reference_time_ + (presentation_time - presentation_time_);
}

}  // namespace fmlib
