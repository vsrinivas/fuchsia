// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_SKIA_TYPE_CONVERTERS_H_
#define APPS_MOZART_LIB_SKIA_TYPE_CONVERTERS_H_

#include "lib/ui/geometry/fidl/geometry.fidl.h"
#include "lib/fidl/cpp/bindings/type_converter.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkMatrix44.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "third_party/skia/include/core/SkRect.h"

// The TypeConverter template is defined in the fidl namespace.
namespace fidl {

template <>
struct TypeConverter<SkIPoint, mozart::Point> {
  static SkIPoint Convert(const mozart::Point& input);
};
template <>
struct TypeConverter<mozart::Point, SkIPoint> {
  static mozart::Point Convert(const SkIPoint& input);
};

template <>
struct TypeConverter<SkPoint, mozart::PointF> {
  static SkPoint Convert(const mozart::PointF& input);
};
template <>
struct TypeConverter<mozart::PointF, SkPoint> {
  static mozart::PointF Convert(const SkPoint& input);
};

template <>
struct TypeConverter<SkIRect, mozart::Rect> {
  static SkIRect Convert(const mozart::Rect& input);
};
template <>
struct TypeConverter<mozart::Rect, SkIRect> {
  static mozart::Rect Convert(const SkIRect& input);
};

template <>
struct TypeConverter<SkRect, mozart::RectF> {
  static SkRect Convert(const mozart::RectF& input);
};
template <>
struct TypeConverter<mozart::RectF, SkRect> {
  static mozart::RectF Convert(const SkRect& input);
};

template <>
struct TypeConverter<SkRect, mozart::RectFPtr> {
  static SkRect Convert(const mozart::RectFPtr& input);
};
template <>
struct TypeConverter<mozart::RectFPtr, SkRect> {
  static mozart::RectFPtr Convert(const SkRect& input);
};

template <>
struct TypeConverter<SkRRect, mozart::RRectF> {
  static SkRRect Convert(const mozart::RRectF& input);
};
template <>
struct TypeConverter<mozart::RRectF, SkRRect> {
  static mozart::RRectF Convert(const SkRRect& input);
};

// Note: This transformation is lossy since Transform is 4x4 whereas
// SkMatrix is only 3x3 so we drop the 3rd row and column.
template <>
struct TypeConverter<SkMatrix, mozart::TransformPtr> {
  static SkMatrix Convert(const mozart::TransformPtr& input);
};
template <>
struct TypeConverter<mozart::TransformPtr, SkMatrix> {
  static mozart::TransformPtr Convert(const SkMatrix& input);
};

// Note: This transformation is lossless.
template <>
struct TypeConverter<SkMatrix44, mozart::TransformPtr> {
  static SkMatrix44 Convert(const mozart::TransformPtr& input);
};
template <>
struct TypeConverter<mozart::TransformPtr, SkMatrix44> {
  static mozart::TransformPtr Convert(const SkMatrix44& input);
};

}  // namespace fidl

#endif  // APPS_MOZART_LIB_SKIA_TYPE_CONVERTERS_H_
