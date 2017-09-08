// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/timeline/fidl_type_conversions.h"

namespace fidl {

media::TimelineTransformPtr
TypeConverter<media::TimelineTransformPtr, media::TimelineFunction>::Convert(
    const media::TimelineFunction& input) {
  media::TimelineTransformPtr result = media::TimelineTransform::New();
  result->reference_time = input.reference_time();
  result->subject_time = input.subject_time();
  result->reference_delta = input.reference_delta();
  result->subject_delta = input.subject_delta();
  return result;
}

media::TimelineFunction
TypeConverter<media::TimelineFunction, media::TimelineTransformPtr>::Convert(
    const media::TimelineTransformPtr& input) {
  return input ? media::TimelineFunction(
                     input->reference_time, input->subject_time,
                     input->reference_delta, input->subject_delta)
               : media::TimelineFunction();
}

}  // namespace fidl
