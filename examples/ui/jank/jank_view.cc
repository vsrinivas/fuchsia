// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/jank/jank_view.h"

#include <unistd.h>

#include <string>

#include "lib/app/cpp/connect.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"

namespace examples {

namespace {
constexpr SkScalar kButtonWidth = 300;
constexpr SkScalar kButtonHeight = 24;
constexpr SkScalar kTextSize = 10;
constexpr SkScalar kMargin = 10;
}  // namespace

const JankView::Button JankView::kButtons[] = {
    {"Hang for 10 seconds", Action::kHang10},
    {"Stutter for 30 seconds", Action::kStutter30},
    {"Crash!", Action::kCrash},
};

JankView::JankView(mozart::ViewManagerPtr view_manager,
                   fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
                   fonts::FontProviderPtr font_provider)
    : SkiaView(std::move(view_manager), std::move(view_owner_request), "Jank"),
      font_loader_(std::move(font_provider)) {
  font_loader_.LoadDefaultFont([this](sk_sp<SkTypeface> typeface) {
    FTL_CHECK(typeface);  // TODO(jeffbrown): Fail gracefully.
    typeface_ = std::move(typeface);
    InvalidateScene();
  });
}

JankView::~JankView() = default;

void JankView::OnSceneInvalidated(
    scenic::PresentationInfoPtr presentation_info) {
  if (!typeface_)
    return;

  SkCanvas* canvas = AcquireCanvas();
  if (!canvas)
    return;
  DrawContent(canvas);
  ReleaseAndSwapCanvas();

  // Stutter if needed.
  if (stutter_end_time_ > ftl::TimePoint::Now())
    sleep(2);

  // Animate.
  InvalidateScene();
}

void JankView::DrawContent(SkCanvas* canvas) {
  SkScalar hsv[3] = {
      static_cast<SkScalar>(
          fmod(ftl::TimePoint::Now().ToEpochDelta().ToSecondsF() * 60, 360.)),
      1, 1};
  canvas->clear(SkHSVToColor(hsv));

  SkScalar x = kMargin;
  SkScalar y = kMargin;
  for (const auto& button : kButtons) {
    DrawButton(canvas, button.label,
               SkRect::MakeXYWH(x, y, kButtonWidth, kButtonHeight));
    y += kButtonHeight + kMargin;
  }
}

void JankView::DrawButton(SkCanvas* canvas,
                          const char* label,
                          const SkRect& bounds) {
  SkPaint boxPaint;
  boxPaint.setColor(SkColorSetRGB(200, 200, 200));
  canvas->drawRect(bounds, boxPaint);
  boxPaint.setColor(SkColorSetRGB(40, 40, 40));
  boxPaint.setStyle(SkPaint::kStroke_Style);
  canvas->drawRect(bounds, boxPaint);

  SkPaint textPaint;
  textPaint.setColor(SK_ColorBLACK);
  textPaint.setTextSize(kTextSize);
  textPaint.setTextEncoding(SkPaint::kUTF8_TextEncoding);
  textPaint.setTypeface(typeface_);
  textPaint.setAntiAlias(true);
  SkRect textBounds;
  textPaint.measureText(label, strlen(label), &textBounds);
  canvas->drawText(label, strlen(label),
                   bounds.centerX() - textBounds.centerX(),
                   bounds.centerY() - textBounds.centerY(), textPaint);
}

bool JankView::OnInputEvent(mozart::InputEventPtr event) {
  if (event->is_pointer()) {
    const mozart::PointerEventPtr& pointer = event->get_pointer();
    if (pointer->phase == mozart::PointerEvent::Phase::DOWN) {
      SkScalar x = pointer->x;
      SkScalar y = pointer->y;
      if (x >= kMargin && x <= kButtonWidth + kMargin) {
        int index = (y - kMargin) / (kButtonHeight + kMargin);
        if (index >= 0 &&
            size_t(index) < sizeof(kButtons) / sizeof(kButtons[0]) &&
            y < (kButtonHeight + kMargin) * (index + 1))
          OnClick(kButtons[index]);
        return true;
      }
    }
  }
  return false;
}

void JankView::OnClick(const Button& button) {
  switch (button.action) {
    case Action::kHang10: {
      sleep(10);
      break;
    }

    case Action::kStutter30: {
      stutter_end_time_ =
          ftl::TimePoint::Now() + ftl::TimeDelta::FromSeconds(30);
      break;
    }

    case Action::kCrash: {
      abort();
      break;
    }
  }
}

}  // namespace examples
