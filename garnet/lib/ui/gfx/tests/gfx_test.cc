// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/gfx_test.h"

#include "garnet/lib/ui/gfx/engine/default_frame_scheduler.h"

namespace scenic_impl {
namespace gfx {
namespace test {

void GfxSystemTest::TearDown() {
  ScenicTest::TearDown();
  engine_.reset();
  frame_scheduler_.reset();
  display_.reset();
  command_buffer_sequencer_.reset();
  FXL_DCHECK(!gfx_system_);
}

void GfxSystemTest::InitializeScenic(Scenic* scenic) {
  FXL_DCHECK(!command_buffer_sequencer_);
  command_buffer_sequencer_ = std::make_unique<escher::impl::CommandBufferSequencer>();
  auto signaller = std::make_unique<ReleaseFenceSignallerForTest>(command_buffer_sequencer_.get());
  display_ = std::make_unique<Display>(
      /*id*/ 0, /* width */ 0, /* height */ 0);
  frame_scheduler_ = std::make_shared<DefaultFrameScheduler>(
      display_.get(),
      std::make_unique<FramePredictor>(gfx::DefaultFrameScheduler::kInitialRenderDuration,
                                       gfx::DefaultFrameScheduler::kInitialUpdateDuration));
  engine_ = std::make_unique<Engine>(frame_scheduler_,
                                     /*display_manager*/ nullptr, std::move(signaller),
                                     escher::EscherWeakPtr());
  frame_scheduler_->SetFrameRenderer(engine_->GetWeakPtr());
  auto system =
      scenic->RegisterSystem<GfxSystem>(display_.get(), engine_.get(), escher::EscherWeakPtr());
  gfx_system_ = system->GetWeakPtr();
  frame_scheduler_->AddSessionUpdater(gfx_system_);
  scenic_->SetInitialized();
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
