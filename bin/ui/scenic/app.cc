// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scenic/app.h"

#include "lib/fsl/tasks/message_loop.h"

#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
#include "garnet/lib/ui/gfx/gfx_system.h"
#endif

#ifdef SCENIC_ENABLE_SKETCHY_SUBSYSTEM
#include "garnet/lib/ui/sketchy/sketchy_system.h"
#endif

#ifdef SCENIC_ENABLE_VIEWS_SUBSYSTEM
#include "garnet/lib/ui/views/view_system.h"
#endif

#ifdef SCENIC_ENABLE_DUMMY_SUBSYSTEM
#include "garnet/lib/ui/scenic/tests/dummy_system.h"
#endif

namespace scenic {

App::App(component::ApplicationContext* app_context)
    : scenic_(std::make_unique<Scenic>(
          app_context,
          fsl::MessageLoop::GetCurrent()->task_runner().get(),
          &clock_)) {
#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
  auto scenic = scenic_->RegisterSystem<scenic::gfx::GfxSystem>();
  FXL_DCHECK(scenic);
#endif

#ifdef SCENIC_ENABLE_SKETCHY_SUBSYSTEM
#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
  auto sketchy = scenic_->RegisterSystem<SketchySystem>(scenic);
  FXL_DCHECK(sketchy);
#else
#error SketchySystem requires gfx::GfxSystem.
#endif
#endif

#ifdef SCENIC_ENABLE_VIEWS_SUBSYSTEM
#ifdef SCENIC_ENABLE_GFX_SUBSYSTEM
  auto views = scenic_->RegisterSystem<ViewSystem>(scenic);
  FXL_DCHECK(views);
#else
#error ViewSystem requires gfx::GfxSystem.
#endif
#endif

#ifdef SCENIC_ENABLE_DUMMY_SUBSYSTEM
  scenic_->RegisterSystem<test::DummySystem>();
#endif
}

}  // namespace scenic
