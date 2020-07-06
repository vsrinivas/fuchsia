// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/tests/gfx_test.h"

#include "src/ui/scenic/lib/scheduling/default_frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/windowed_frame_predictor.h"

namespace scenic_impl {
namespace gfx {
namespace test {

void GfxSystemTest::TearDown() {
  ScenicTest::TearDown();
  engine_.reset();
  frame_scheduler_.reset();
  FX_DCHECK(gfx_system_.expired());
}

void GfxSystemTest::InitializeScenic(Scenic* scenic) {
  auto signaller = std::make_unique<ReleaseFenceSignallerForTest>();
  frame_scheduler_ = std::make_shared<scheduling::DefaultFrameScheduler>(
      std::make_shared<scheduling::VsyncTiming>(),
      std::make_unique<scheduling::WindowedFramePredictor>(
          scheduling::DefaultFrameScheduler::kMinPredictedFrameDuration,
          scheduling::DefaultFrameScheduler::kInitialRenderDuration,
          scheduling::DefaultFrameScheduler::kInitialUpdateDuration));
  engine_ = std::make_shared<Engine>(context_provider_.context(), frame_scheduler_,
                                     std::move(signaller), escher::EscherWeakPtr());
  frame_scheduler_->SetFrameRenderer(engine_);
  gfx_system_ = scenic->RegisterSystem<GfxSystem>(engine_.get(),
                                                  /* sysmem */ nullptr,
                                                  /* display_manager */ nullptr);
  frame_scheduler_->AddSessionUpdater(gfx_system_);
  scenic_->SetInitialized(engine_->scene_graph());
  scenic_->SetFrameScheduler(frame_scheduler_);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
