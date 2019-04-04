// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/sketchy/view.h"

#include <trace/event.h>

namespace sketchy_example {

namespace {

// TODO(SCN-1278): Remove this.
// Turns two floats (high bits, low bits) into a 64-bit uint.
trace_flow_id_t PointerTraceHACK(float fa, float fb) {
  uint32_t ia, ib;
  memcpy(&ia, &fa, sizeof(uint32_t));
  memcpy(&ib, &fb, sizeof(uint32_t));
  return (((uint64_t)ia) << 32) | ib;
}

}

View::View(scenic::ViewContext context, async::Loop* loop)
    : BaseView(std::move(context), "Sketchy Example"),
      canvas_(startup_context(), loop),
      background_node_(session()),
      import_node_holder_(session()),
      import_node_(&canvas_, import_node_holder_),
      scratch_group_(&canvas_),
      stable_group_(&canvas_) {
  root_node().AddChild(background_node_);
  scenic::Material background_material(session());
  background_material.SetColor(220, 220, 220, 255);
  background_node_.SetMaterial(background_material);

  root_node().AddChild(import_node_holder_);
  import_node_holder_.SetTranslation(0.f, 0.f, -50.f);

  import_node_.AddChild(scratch_group_);
  import_node_.AddChild(stable_group_);

  fuchsia::ui::input::SetHardKeyboardDeliveryCmd cmd;
  cmd.delivery_request = true;
  fuchsia::ui::input::Command input_cmd;
  input_cmd.set_set_hard_keyboard_delivery(std::move(cmd));
  session()->Enqueue(std::move(input_cmd));
}

void View::OnPropertiesChanged(
    fuchsia::ui::gfx::ViewProperties old_properties) {
  float width = view_properties().bounding_box.max.x -
                view_properties().bounding_box.min.x;
  float height = view_properties().bounding_box.max.y -
                 view_properties().bounding_box.min.y;

  scenic::Rectangle background_shape(session(), width, height);
  background_node_.SetShape(background_shape);
  background_node_.SetTranslation(width * .5f, height * .5f, -.1f);
  canvas_.Present(zx_clock_get_monotonic(),
                  [](fuchsia::images::PresentationInfo info) {});
}

void View::OnInputEvent(fuchsia::ui::input::InputEvent event) {
  TRACE_DURATION("gfx", "View::OnInputEvent");
  if (event.is_pointer()) {
    const auto& pointer = event.pointer();
    const trace_flow_id_t trace_id =
        PointerTraceHACK(pointer.radius_major, pointer.radius_minor);
    TRACE_FLOW_END("input", "dispatch_event_to_client", trace_id);
    switch (pointer.phase) {
      case fuchsia::ui::input::PointerEventPhase::DOWN: {
        auto stroke = fxl::MakeRefCounted<sketchy_lib::Stroke>(&canvas_);
        pointer_id_to_stroke_map_.insert({pointer.pointer_id, stroke});
        scratch_group_.AddStroke(*stroke);
        stroke->Begin({pointer.x, pointer.y});
        canvas_.Present(zx_clock_get_monotonic(),
                        [](fuchsia::images::PresentationInfo info) {});
        return;
      }
      case fuchsia::ui::input::PointerEventPhase::MOVE: {
        const auto& stroke =
            pointer_id_to_stroke_map_.find(pointer.pointer_id)->second;
        if (!stroke) {
          return;
        }
        stroke->Extend({{pointer.x, pointer.y}});
        canvas_.Present(zx_clock_get_monotonic(),
                        [](fuchsia::images::PresentationInfo info) {});
        return;
      }
      case fuchsia::ui::input::PointerEventPhase::UP: {
        auto it = pointer_id_to_stroke_map_.find(pointer.pointer_id);
        const auto& stroke = it->second;
        if (!stroke) {
          return;
        }
        stroke->Finish();
        scratch_group_.RemoveStroke(*stroke);
        stable_group_.AddStroke(*stroke);
        pointer_id_to_stroke_map_.erase(it);
        canvas_.Present(zx_clock_get_monotonic(),
                        [](fuchsia::images::PresentationInfo info) {});
        return;
      }
      default:
        break;
    }
  }

  if (event.is_keyboard()) {
    const auto& keyboard = event.keyboard();
    if (keyboard.phase == fuchsia::ui::input::KeyboardEventPhase::PRESSED &&
        keyboard.hid_usage == 6 /* c */) {
      stable_group_.Clear();
      canvas_.Present(zx_clock_get_monotonic(),
                      [](fuchsia::images::PresentationInfo info) {});
      return;
    }
  }

  return;
}

}  // namespace sketchy_example
