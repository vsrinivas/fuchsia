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
  command_buffer_sequencer_.reset();
  FXL_DCHECK(!gfx_system_);
}

void GfxSystemTest::InitializeScenic(Scenic* scenic) {
  FXL_DCHECK(!command_buffer_sequencer_);
  command_buffer_sequencer_ = std::make_unique<escher::impl::CommandBufferSequencer>();
  auto signaller = std::make_unique<ReleaseFenceSignallerForTest>(command_buffer_sequencer_.get());
  frame_scheduler_ = std::make_shared<scheduling::DefaultFrameScheduler>(
      std::make_shared<scheduling::VsyncTiming>(),
      std::make_unique<scheduling::WindowedFramePredictor>(
          scheduling::DefaultFrameScheduler::kInitialRenderDuration,
          scheduling::DefaultFrameScheduler::kInitialUpdateDuration));
  engine_ = std::make_unique<Engine>(context_provider_.context(), frame_scheduler_,
                                     std::move(signaller), escher::EscherWeakPtr());
  frame_scheduler_->SetFrameRenderer(engine_->GetWeakPtr());
  auto system = scenic->RegisterSystem<GfxSystem>(engine_.get(), escher::EscherWeakPtr(),
                                                  /* sysmem */ nullptr,
                                                  /* display_manager */ nullptr);
  gfx_system_ = system->GetWeakPtr();
  frame_scheduler_->AddSessionUpdater(gfx_system_);
  scenic_->SetInitialized(engine_->scene_graph());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
