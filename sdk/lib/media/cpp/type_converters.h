// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MEDIA_CPP_TYPE_CONVERTERS_H_
#define LIB_MEDIA_CPP_TYPE_CONVERTERS_H_

#include <fuchsia/media/cpp/fidl.h>

#include "lib/fidl/cpp/type_converter.h"
#include "lib/media/cpp/timeline_function.h"

namespace fidl {

template <>
struct TypeConverter<fuchsia::media::TimelineFunction,
                     media::TimelineFunction> {
  static fuchsia::media::TimelineFunction Convert(
      const media::TimelineFunction& value);
};

template <>
struct TypeConverter<media::TimelineFunction,
                     fuchsia::media::TimelineFunction> {
  static media::TimelineFunction Convert(
      const fuchsia::media::TimelineFunction& value);
};

}  // namespace fidl

#endif  // LIB_MEDIA_CPP_TYPE_CONVERTERS_H_
