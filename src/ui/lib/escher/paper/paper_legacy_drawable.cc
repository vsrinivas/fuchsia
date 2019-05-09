// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/paper/paper_legacy_drawable.h"

#include "src/ui/lib/escher/paper/paper_draw_call_factory.h"
#include "src/ui/lib/escher/paper/paper_shape_cache.h"
#include "src/ui/lib/escher/paper/paper_transform_stack.h"
#include "src/ui/lib/escher/util/trace_macros.h"

namespace escher {

void PaperLegacyDrawable::DrawInScene(const PaperScene* scene,
                                      PaperDrawCallFactory* draw_call_factory,
                                      PaperTransformStack* transform_stack,
                                      Frame* frame, PaperDrawableFlags flags) {
  TRACE_DURATION("gfx", "PaperLegacyDrawable::DrawInScene");

  if (!object_.material())
    return;

  const auto& material = *object_.material().get();

  FXL_DCHECK(!object_.shape().modifiers());
  FXL_DCHECK(object_.clippers().empty());
  FXL_DCHECK(object_.clippees().empty());

  transform_stack->PushTransform(object_.transform());

  switch (object_.shape().type()) {
    case Shape::Type::kRect: {
      draw_call_factory->DrawRect(vec2(0, 0), vec2(1, 1), material, flags);
    } break;
    case Shape::Type::kCircle: {
      draw_call_factory->DrawCircle(1, material, flags);
    } break;
    case Shape::Type::kMesh: {
      draw_call_factory->EnqueueDrawCalls(
          PaperShapeCacheEntry{
              .mesh = object_.shape().mesh(),
              .num_indices = object_.shape().mesh()->num_indices(),
              .num_shadow_volume_indices = 0,
          },
          material, flags);
    } break;
    case Shape::Type::kNone: {
    } break;
  }

  transform_stack->Pop();
}

}  // namespace escher
