// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/mozart/app.h"

#include "lib/fsl/tasks/message_loop.h"

#ifdef MOZART_ENABLE_SCENIC_SUBSYSTEM
#error not implemented
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
#error not implemented
#endif

#ifdef MOZART_ENABLE_DUMMY_SUBSYSTEM
  mozart_->RegisterSystem<test::DummySystem>();
#endif
}

}  // namespace mz
