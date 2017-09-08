// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/noodles/noodles_view.h"

#include <math.h>

#include <cstdlib>
#include <utility>

#include "lib/ftl/logging.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkPath.h"

namespace examples {

namespace {
constexpr float kSecondsBetweenChanges = 10.f;
constexpr float kSpeed = 1.f;
constexpr float kSecondsPerNanosecond = .000'000'001f;

void Lissajous(SkPath* path, double ax, double ay, int wx, int wy, double p) {
  uint32_t segments = ceil(fabs(ax) + fabs(ay)) / 2u + 1u;
  for (uint32_t i = 0; i < segments; ++i) {
    double t = M_PI * 2.0 * i / segments;
    double x = ax * sin(t * wx);
    double y = ay * sin(t * wy + p);
    if (i == 0u)
      path->moveTo(x, y);
    else
      path->lineTo(x, y);
  }
  path->close();
}
}  // namespace

NoodlesView::NoodlesView(
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
    : SkiaView(std::move(view_manager),
               std::move(view_owner_request),
               "Noodles") {}

NoodlesView::~NoodlesView() {}

void NoodlesView::OnSceneInvalidated(
    scenic::PresentationInfoPtr presentation_info) {
  SkCanvas* canvas = AcquireCanvas();
  if (!canvas)
    return;

  // Update the animation state.
  uint64_t presentation_time = presentation_info->presentation_time;
  if (!start_time_ ||
      presentation_time - start_time_ >= MX_SEC(kSecondsBetweenChanges)) {
    start_time_ = presentation_time;
    wx_ = rand() % 9 + 1;
    wy_ = rand() % 9 + 1;
  }
  const float phase =
      (presentation_time - start_time_) * kSecondsPerNanosecond * kSpeed;
  Draw(canvas, phase);
  ReleaseAndSwapCanvas();

  // Animate.
  InvalidateScene();
}

void NoodlesView::Draw(SkCanvas* canvas, float phase) {
  constexpr int count = 4;
  constexpr int padding = 1;

  canvas->clear(SK_ColorBLACK);

  double cx = logical_size().width * 0.5;
  double cy = logical_size().height * 0.5;
  canvas->translate(cx, cy);

  for (int i = 0; i < count; i++, phase += 0.1) {
    SkPaint paint;
    SkScalar hsv[3] = {static_cast<SkScalar>(fmod(phase * 120, 360)), 1, 1};
    paint.setColor(SkHSVToColor(hsv));
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setAntiAlias(true);

    SkPath path;
    Lissajous(&path, cx - padding, cy - padding, wx_, wy_, phase);
    canvas->drawPath(path, paint);
  }
}

}  // namespace examples
