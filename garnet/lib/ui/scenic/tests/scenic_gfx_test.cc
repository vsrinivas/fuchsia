// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/tests/scenic_gfx_test.h"

#include <lib/async-testing/test_loop.h>

#include "garnet/lib/ui/gfx/engine/default_frame_scheduler.h"
#include "garnet/lib/ui/gfx/tests/mocks.h"

namespace scenic_impl {
namespace test {

void ScenicGfxTest::InitializeScenic(Scenic* scenic) {
  command_buffer_sequencer_ = std::make_unique<escher::impl::CommandBufferSequencer>();
  auto signaller =
      std::make_unique<gfx::test::ReleaseFenceSignallerForTest>(command_buffer_sequencer_.get());
  display_ = std::make_unique<gfx::Display>(
      /*id*/ 0, /* width */ 0, /* height */ 0);

  // TODO(SCN-421)): This frame scheduler is only needed for a single test in scenic_unittests.cc.
  // When this bug is fixed, that test will no longer depend on a GfxSystem, at which point, this
  // frame scheduler can be removed.
  frame_scheduler_ = std::make_shared<gfx::DefaultFrameScheduler>(
      display_.get(),
      std::make_unique<gfx::FramePredictor>(gfx::DefaultFrameScheduler::kInitialRenderDuration,
                                            gfx::DefaultFrameScheduler::kInitialUpdateDuration),
      scenic_->inspect_node()->CreateChild("FrameScheduler"));

  engine_ = std::make_unique<gfx::Engine>(frame_scheduler_,
                                          /*display_manager*/ nullptr, std::move(signaller),
                                          escher::EscherWeakPtr());
  auto system = scenic->RegisterSystem<gfx::GfxSystem>(display_.get(), engine_.get(),
                                                       escher::EscherWeakPtr());
  scenic->SetInitialized();

  RunLoopUntilIdle();  // Finish initialization
}

void ScenicGfxTest::TearDown() {
  ScenicTest::TearDown();
  display_.reset();
  engine_.reset();
  command_buffer_sequencer_.reset();
}

}  // namespace test
}  // namespace scenic_impl
