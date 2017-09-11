// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/paint/paint_view.h"

#include <hid/usages.h>

#include "lib/app/cpp/connect.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRect.h"

namespace examples {

PaintView::PaintView(
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
    : SkiaView(std::move(view_manager),
               std::move(view_owner_request),
               "Paint") {}

PaintView::~PaintView() = default;

void PaintView::OnSceneInvalidated(
    scenic::PresentationInfoPtr presentation_info) {
  SkCanvas* canvas = AcquireCanvas();
  if (!canvas)
    return;

  DrawContent(canvas);
  ReleaseAndSwapCanvas();
}

void PaintView::DrawContent(SkCanvas* canvas) {
  canvas->clear(SK_ColorWHITE);

  SkPaint paint;
  paint.setColor(0xFFFF00FF);
  paint.setAntiAlias(true);
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(SkIntToScalar(3));

  for (auto path : paths_) {
    canvas->drawPath(path, paint);
  }

  paint.setColor(SK_ColorBLUE);
  for (auto iter = points_.begin(); iter != points_.end(); ++iter) {
    if (!iter->second.empty()) {
      canvas->drawPath(CurrentPath(iter->first), paint);
    }
  }
}

SkPath PaintView::CurrentPath(uint32_t pointer_id) {
  SkPath path;
  if (points_.count(pointer_id)) {
    uint32_t count = 0;
    for (auto point : points_.at(pointer_id)) {
      if (count++ == 0) {
        path.moveTo(point);
      } else {
        path.lineTo(point);
      }
    }
  }
  return path;
}

bool PaintView::OnInputEvent(mozart::InputEventPtr event) {
  bool handled = false;
  if (event->is_pointer()) {
    const mozart::PointerEventPtr& pointer = event->get_pointer();
    uint32_t pointer_id = pointer->device_id * 32 + pointer->pointer_id;
    switch (pointer->phase) {
      case mozart::PointerEvent::Phase::DOWN:
      case mozart::PointerEvent::Phase::MOVE:
        // On down + move, keep appending points to the path being built
        // For mouse only draw if left button is pressed
        if (pointer->type == mozart::PointerEvent::Type::TOUCH ||
            pointer->type == mozart::PointerEvent::Type::STYLUS ||
            (pointer->type == mozart::PointerEvent::Type::MOUSE &&
             pointer->buttons & mozart::kMousePrimaryButton)) {
          if (!points_.count(pointer_id)) {
            points_[pointer_id] = std::vector<SkPoint>();
          }
          points_.at(pointer_id)
              .push_back(SkPoint::Make(pointer->x, pointer->y));
        }
        handled = true;
        break;
      case mozart::PointerEvent::Phase::UP:
        // Path is done, add it to the list of paths and reset the list of
        // points
        paths_.push_back(CurrentPath(pointer_id));
        points_.erase(pointer_id);
        handled = true;
        break;
      default:
        break;
    }
  } else if (event->is_keyboard()) {
    const mozart::KeyboardEventPtr& keyboard = event->get_keyboard();
    if (keyboard->hid_usage == HID_USAGE_KEY_ESC) {
      // clear
      paths_.clear();
      handled = true;
    }
  }

  InvalidateScene();
  return handled;
}

}  // namespace examples
