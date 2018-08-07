// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MEDIA_TIMELINE_TYPE_CONVERTERS_H_
#define LIB_MEDIA_TIMELINE_TYPE_CONVERTERS_H_

#include <fuchsia/mediaplayer/cpp/fidl.h>

#include "lib/fxl/type_converter.h"
#include "lib/media/timeline/timeline_function.h"

namespace fxl {

template <>
struct TypeConverter<fuchsia::mediaplayer::TimelineFunction,
                     media::TimelineFunction> {
  static fuchsia::mediaplayer::TimelineFunction Convert(
      const media::TimelineFunction& value);
};

template <>
struct TypeConverter<media::TimelineFunction,
                     fuchsia::mediaplayer::TimelineFunction> {
  static media::TimelineFunction Convert(
      const fuchsia::mediaplayer::TimelineFunction& value);
};

}  // namespace fxl

#endif  // LIB_MEDIA_TIMELINE_TYPE_CONVERTERS_H_
