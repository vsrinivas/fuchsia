// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scenic/app.h"

#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
#include "garnet/lib/ui/gfx/gfx_system.h"
#endif

#ifdef SCENIC_ENABLE_SKETCHY_SUBSYSTEM
#include "garnet/lib/ui/sketchy/sketchy_system.h"
#endif

#ifdef SCENIC_ENABLE_VIEWS_SUBSYSTEM
#include "garnet/lib/ui/views/view_system.h"
#endif

#ifdef SCENIC_ENABLE_INPUT_SUBSYSTEM
#include "garnet/lib/ui/a11y_input/a11y_input_system.h"
#include "garnet/lib/ui/input/input_system.h"
#endif

namespace scenic_impl {

App::App(sys::ComponentContext* app_context, inspect::Object inspect_object,
         fit::closure quit_callback)
    : scenic_(std::make_unique<Scenic>(app_context, std::move(inspect_object),
                                       std::move(quit_callback))) {
#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
  auto gfx = scenic_->RegisterSystem<gfx::GfxSystem>(
      std::make_unique<gfx::DisplayManager>());
  FXL_DCHECK(gfx);
#endif

#ifdef SCENIC_ENABLE_SKETCHY_SUBSYSTEM
#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
  auto sketchy = scenic_->RegisterSystem<SketchySystem>(gfx);
  FXL_DCHECK(sketchy);
#else
#error SketchySystem requires gfx::GfxSystem.
#endif
#endif

#ifdef SCENIC_ENABLE_INPUT_SUBSYSTEM
#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
  auto input = scenic_->RegisterSystem<input::InputSystem>(gfx);
  FXL_DCHECK(input);

  auto a11y_input = scenic_->RegisterSystem<a11y_input::A11yInputSystem>(gfx);
  FXL_DCHECK(a11y_input);
#else
#error InputSystem requires gfx::GfxSystem.
#endif
#endif
}

}  // namespace scenic_impl
