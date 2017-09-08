// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/media/timeline/timeline_function.h"
#include "lib/media/fidl/timelines.fidl.h"

namespace fidl {

template <>
struct TypeConverter<media::TimelineTransformPtr, media::TimelineFunction> {
  static media::TimelineTransformPtr Convert(
      const media::TimelineFunction& input);
};

template <>
struct TypeConverter<media::TimelineFunction, media::TimelineTransformPtr> {
  static media::TimelineFunction Convert(
      const media::TimelineTransformPtr& input);
};

}  // namespace fidl
