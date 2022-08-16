// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/tests/gfx_test.h"

#include "src/ui/scenic/lib/scheduling/default_frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/windowed_frame_predictor.h"

namespace scenic_impl::gfx::test {

void GfxSystemTest::TearDown() {
  ScenicTest::TearDown();
  engine_.reset();
  FX_DCHECK(gfx_system_.expired());
}

void GfxSystemTest::InitializeScenic(std::shared_ptr<Scenic> scenic) {
  engine_ = std::make_shared<Engine>(escher::EscherWeakPtr());
  auto image_pipe_updater = std::make_shared<ImagePipeUpdater>(*frame_scheduler_);
  gfx_system_ =
      scenic->RegisterSystem<GfxSystem>(engine_.get(),
                                        /* sysmem */ nullptr,
                                        /* display_manager */ nullptr, image_pipe_updater);
  frame_scheduler_->Initialize(std::make_shared<scheduling::VsyncTiming>(),
                               /*frame_renderer*/ engine_,
                               /*session_updaters*/ {image_pipe_updater, scenic});
}

}  // namespace scenic_impl::gfx::test
