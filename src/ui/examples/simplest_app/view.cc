// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/simplest_app/view.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace simplest_app {

using ::fuchsia::ui::input::InputEvent;
using ::fuchsia::ui::input::KeyboardEventPhase;
using ::fuchsia::ui::input::PointerEventPhase;

namespace {

// TODO(fxbug.dev/24476): Remove this.
// Turns two floats (high bits, low bits) into a 64-bit uint.
trace_flow_id_t PointerTraceHACK(float fa, float fb) {
  uint32_t ia, ib;
  memcpy(&ia, &fa, sizeof(uint32_t));
  memcpy(&ib, &fb, sizeof(uint32_t));
  return (((uint64_t)ia) << 32) | ib;
}

// Helper for OnInputEvent: respond to pointer events.
scenic::Material NextColor(scenic::Session* session) {
  static uint8_t red = 128, green = 128, blue = 128;
  scenic::Material material(session);
  material.SetColor(red, green, blue, 255);
  red += 16;
  green += 32;
  blue += 64;
  return material;
}

}  // namespace

SimplestAppView::SimplestAppView(scenic::ViewContext context, async::Loop* message_loop)
    : scenic::BaseView(std::move(context), "simplest_app SimplestAppView"),
      message_loop_(message_loop),
      background_(session()),
      focused_(false) {
  FX_CHECK(message_loop_);

  root_node().AddChild(background_);

  scenic::Material background_material(session());
  background_material.SetColor(30, 30, 120, 255);
  background_.SetMaterial(background_material);

  fuchsia::ui::input::SetHardKeyboardDeliveryCmd cmd;
  cmd.delivery_request = true;
  fuchsia::ui::input::Command input_cmd;
  input_cmd.set_set_hard_keyboard_delivery(std::move(cmd));
  session()->Enqueue(std::move(input_cmd));
  // Consider breaking out into a discrete initializer if more work is added.
}

void SimplestAppView::OnPropertiesChanged(fuchsia::ui::gfx::ViewProperties old_properties) {
  if (view_holder_) {
    view_holder_->SetViewProperties(view_properties());
  }

  UpdateBackground();
}

void SimplestAppView::OnInputEvent(fuchsia::ui::input::InputEvent event) {
  TRACE_DURATION("gfx", "SimplestAppView::OnInputEvent");

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
            UpdateBackground();
          }
          break;
        }
        default:
          break;  // Ignore all other pointer phases.
      }
      break;
    }
    case InputEvent::Tag::kKeyboard: {
      const auto& key = event.keyboard();
      if (key.hid_usage == /* Esc key*/ 0x29 && key.phase == KeyboardEventPhase::RELEASED) {
        async::PostTask(message_loop_->dispatcher(), [this] { message_loop_->Quit(); });
      }
      break;
    }
    case InputEvent::Tag::Invalid: {
      FX_NOTREACHED();
      break;
    }
  }
}

void SimplestAppView::UpdateBackground() {
  if (!has_logical_size()) {
    return;
  }

  const auto size = logical_size();
  const float width = size.x;
  const float height = size.y;

  scenic::RoundedRectangle background_shape(session(), width, height, 20, 20, 80, 10);
  background_.SetMaterial(NextColor(session()));
  background_.SetShape(background_shape);
  background_.SetTranslation(width / 2.f, height / 2.f, -10.f);
  PresentScene();
}

}  // namespace simplest_app
