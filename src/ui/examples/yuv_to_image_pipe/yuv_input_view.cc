// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/yuv_to_image_pipe/yuv_input_view.h"

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/trace/event.h>

namespace yuv_to_image_pipe {

namespace {

constexpr int kNumImages = 3;

// TODO(fxbug.dev/24476): Remove this.
// Turns two floats (high bits, low bits) into a 64-bit uint.
trace_flow_id_t PointerTraceHACK(float fa, float fb) {
  uint32_t ia, ib;
  memcpy(&ia, &fa, sizeof(uint32_t));
  memcpy(&ib, &fb, sizeof(uint32_t));
  return (((uint64_t)ia) << 32) | ib;
}

}  // namespace

using ::fuchsia::ui::input::InputEvent;
using ::fuchsia::ui::input::KeyboardEventPhase;
using ::fuchsia::ui::input::PointerEventPhase;

YuvInputView::YuvInputView(scenic::ViewContext context,
                           fuchsia::sysmem::PixelFormatType pixel_format)
    : YuvBaseView(std::move(context), pixel_format) {
  for (int i = 0; i < kNumImages; ++i) {
    image_ids_.push_back(AddImage());
    PaintImage(image_ids_.back(), GetNextPixelMultiplier());
  }
  PresentImage(GetNextImageId());
}

void YuvInputView::OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size()) {
    return;
  }
  node_.SetTranslation(logical_size().x * 0.5f, logical_size().y * 0.5f, 0);
}

void YuvInputView::OnInputEvent(fuchsia::ui::input::InputEvent event) {
  TRACE_DURATION("gfx", "YuvInputView::OnInputEvent");

  switch (event.Which()) {
    case InputEvent::Tag::kFocus: {
      focused_ = event.focus().focused;
      break;
    }
    case InputEvent::Tag::kPointer: {
      const auto& pointer = event.pointer();
      trace_flow_id_t trace_id = PointerTraceHACK(pointer.radius_major, pointer.radius_minor);
      TRACE_FLOW_END("input", "dispatch_event_to_client", trace_id);
      switch (pointer.phase) {
        case PointerEventPhase::DOWN: {
          if (focused_) {
            const auto next_image_id_ = GetNextImageId();
            PaintImage(next_image_id_, GetNextPixelMultiplier());
            PresentImage(next_image_id_);
          }
          break;
        }
        default:
          break;  // Ignore all other pointer phases.
      }
      break;
    }
    case InputEvent::Tag::kKeyboard: {
      break;
    }
    case InputEvent::Tag::Invalid: {
      FX_NOTREACHED();
      break;
    }
  }
}

uint32_t YuvInputView::GetNextImageId() {
  const auto rv = image_ids_[next_image_index_];
  next_image_index_ = ++next_image_index_ % image_ids_.size();
  return rv;
}

uint8_t YuvInputView::GetNextPixelMultiplier() {
  pixel_multiplier_ = pixel_multiplier_ ? pixel_multiplier_ - 10 : 255;
  return pixel_multiplier_;
}

}  // namespace yuv_to_image_pipe
