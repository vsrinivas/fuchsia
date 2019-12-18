// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_HELPER_H_
#define SRC_UI_SCENIC_LIB_INPUT_HELPER_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>

#include "src/ui/scenic/lib/gfx/gfx_system.h"

namespace scenic_impl {
namespace input {

// Creates a ray pointing into the screen at position (|x|, |y|).
escher::ray4 CreateScreenPerpendicularRay(float x, float y);

// Clone |event| and set its coordinates to |coords|.
fuchsia::ui::input::PointerEvent ClonePointerWithCoords(
    const fuchsia::ui::input::PointerEvent& event, const escher::vec2& coords);

// Extracts the coordinates from |event|.
escher::vec2 PointerCoords(const fuchsia::ui::input::PointerEvent& event);

// Helper for Dispatch[Touch|Mouse]Command.
escher::vec2 TransformPointerCoords(const escher::vec2& pointer, const glm::mat4 transform);

// Finds (Vulkan) normalized device coordinates with respect to the (single) layer.
escher::vec2 NormalizePointerCoords(const escher::vec2& pointer,
                                    const gfx::LayerStackPtr& layer_stack);

// Builds a pointer event with local (described by |transform|) view coordinates.
fuchsia::ui::input::PointerEvent BuildLocalPointerEvent(
    const fuchsia::ui::input::PointerEvent& pointer_event, const glm::mat4& transform);

}  // namespace input
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_INPUT_HELPER_H_
