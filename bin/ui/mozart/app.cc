// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/mozart/app.h"

#include "lib/fsl/tasks/message_loop.h"

#ifdef MOZART_ENABLE_SCENIC_SUBSYSTEM
#include "garnet/lib/ui/scenic/scenic_system.h"
#endif

#ifdef MOZART_ENABLE_VIEWS_SUBSYSTEM
#include "garnet/lib/ui/views/view_system.h"
#endif

#ifdef MOZART_ENABLE_DUMMY_SUBSYSTEM
#include "garnet/lib/ui/mozart/tests/dummy_system.h"
#endif

namespace mz {

App::App(app::ApplicationContext* app_context)
    : mozart_(std::make_unique<Mozart>(
          app_context,
          fsl::MessageLoop::GetCurrent()->task_runner().get(),
          &clock_)) {
#ifdef MOZART_ENABLE_SCENIC_SUBSYSTEM
  auto scenic = mozart_->RegisterSystem<scene_manager::ScenicSystem>();
  FXL_DCHECK(scenic);
#endif

#ifdef MOZART_ENABLE_VIEWS_SUBSYSTEM
#ifdef MOZART_ENABLE_SCENIC_SUBSYSTEM
  auto views = mozart_->RegisterSystem<mz::ViewSystem>(scenic);
  FXL_DCHECK(views);
#else
#error Mozart Views require Scenic.
#endif
#endif

#ifdef MOZART_ENABLE_DUMMY_SUBSYSTEM
  mozart_->RegisterSystem<test::DummySystem>();
#endif
}

}  // namespace mz
