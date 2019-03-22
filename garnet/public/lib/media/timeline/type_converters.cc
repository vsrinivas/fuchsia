// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/timeline/type_converters.h"

namespace fidl {

// static
fuchsia::media::TimelineFunction TypeConverter<
    fuchsia::media::TimelineFunction,
    media::TimelineFunction>::Convert(const media::TimelineFunction& value) {
  fuchsia::media::TimelineFunction result;
  result.subject_time = value.subject_time();
  result.reference_time = value.reference_time();
  result.subject_delta = value.subject_delta();
  result.reference_delta = value.reference_delta();
  return result;
}

// static
media::TimelineFunction
TypeConverter<media::TimelineFunction, fuchsia::media::TimelineFunction>::
    Convert(const fuchsia::media::TimelineFunction& value) {
  return media::TimelineFunction(value.subject_time, value.reference_time,
                                 value.subject_delta, value.reference_delta);
}

}  // namespace fidl
