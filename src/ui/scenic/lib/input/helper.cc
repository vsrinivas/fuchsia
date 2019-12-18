// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/helper.h"

#include "src/ui/lib/escher/util/type_utils.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"

namespace scenic_impl {
namespace input {

using fuchsia::ui::input::PointerEvent;

// LINT.IfChange
escher::ray4 CreateScreenPerpendicularRay(float x, float y) {
  // We set the elevation for the origin point, and Z value for the direction,
  // such that we start above the scene and point into the scene.
  //
  // For hit testing, these values work in conjunction with
  // Camera::ProjectRayIntoScene to create an appropriate ray4 that works
  // correctly with the hit tester.
  //
  // TODO(38389): Scenic used to surface left-handed z, so |layer.cc| contains vestigial logic that
  // flips z. As such, "taking a step back" translates to "positive Z origin" and "look at the
  // scene" translates to "negative Z direction". We should be able to remove that flip and restore
  // Vulkan's z-in semantics. Similarly since hit testing originates from the camera it should not
  // be necessary to step back from the camera for the hit ray.
  //
  // During hit testing, we translate an arbitrary pointer's (x,y) device-space
  // coordinates to a View's (x', y') model-space coordinates.
  return {{x, y, 1, 1},  // Origin as homogeneous point.
          {0, 0, -1, 0}};
}
// LINT.ThenChange(//src/ui/scenic/lib/gfx/tests/hittest_global_unittest.cc)

PointerEvent ClonePointerWithCoords(const PointerEvent& event, const escher::vec2& coords) {
  PointerEvent clone;
  fidl::Clone(event, &clone);
  clone.x = coords.x;
  clone.y = coords.y;
  return clone;
}

escher::vec2 PointerCoords(const PointerEvent& event) { return {event.x, event.y}; }

escher::vec2 TransformPointerCoords(const escher::vec2& pointer, const glm::mat4 transform) {
  const escher::ray4 screen_ray = CreateScreenPerpendicularRay(pointer.x, pointer.y);
  const escher::ray4 local_ray = transform * screen_ray;

  // We treat distance as 0 to simplify; otherwise the formula is:
  // hit = homogenize(local_ray.origin + distance * local_ray.direction);
  escher::vec2 hit(escher::homogenize(local_ray.origin));

  FXL_VLOG(2) << "Coordinate transform (device->view): (" << screen_ray.origin.x << ", "
              << screen_ray.origin.y << ")->(" << hit.x << ", " << hit.y << ")";
  return hit;
}

// Finds (Vulkan) normalized device coordinates with respect to the (single) layer. This is intended
// for magnification gestures.
escher::vec2 NormalizePointerCoords(const escher::vec2& pointer,
                                    const gfx::LayerStackPtr& layer_stack) {
  if (layer_stack->layers().empty()) {
    return {0, 0};
  }

  // RootPresenter only owns one layer per presentation/layer stack. To support multiple layers,
  // we'd need to partition the input space. So, for now to simplify things we'll treat the layer
  // size as display dimensions, and if we ever find more than one layer in a stack, we should
  // worry.
  FXL_DCHECK(layer_stack->layers().size() == 1)
      << "Multiple GFX layers; multi-layer input dispatch not implemented.";
  const gfx::Layer& layer = **layer_stack->layers().begin();

  return {
      layer.width() > 0 ? 2.f * pointer.x / layer.width() - 1 : 0,
      layer.height() > 0 ? 2.f * pointer.y / layer.height() - 1 : 0,
  };
}

PointerEvent BuildLocalPointerEvent(const PointerEvent& pointer_event, const glm::mat4& transform) {
  return ClonePointerWithCoords(pointer_event,
                                TransformPointerCoords(PointerCoords(pointer_event), transform));
}

}  // namespace input
}  // namespace scenic_impl
