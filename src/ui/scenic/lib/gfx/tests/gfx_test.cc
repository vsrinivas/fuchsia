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
  frame_scheduler_.reset();
  FX_DCHECK(gfx_system_.expired());
}

void GfxSystemTest::InitializeScenic(std::shared_ptr<Scenic> scenic) {
  frame_scheduler_ = std::make_shared<scheduling::DefaultFrameScheduler>(
      std::make_shared<scheduling::VsyncTiming>(),
      std::make_unique<scheduling::WindowedFramePredictor>(
          scheduling::DefaultFrameScheduler::kMinPredictedFrameDuration,
          scheduling::DefaultFrameScheduler::kInitialRenderDuration,
          scheduling::DefaultFrameScheduler::kInitialUpdateDuration));
  engine_ = std::make_shared<Engine>(escher::EscherWeakPtr());
  auto image_pipe_updater = std::make_shared<ImagePipeUpdater>(frame_scheduler_);
  gfx_system_ =
      scenic->RegisterSystem<GfxSystem>(engine_.get(),
                                        /* sysmem */ nullptr,
                                        /* display_manager */ nullptr, image_pipe_updater);
  scenic->SetFrameScheduler(frame_scheduler_);
  frame_scheduler_->Initialize(/*frame_renderer*/ engine_,
                               /*session_updaters*/ {image_pipe_updater, scenic});
}

}  // namespace scenic_impl::gfx::test
