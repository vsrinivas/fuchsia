// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scenic/tests/scenic_gfx_test.h"

#include <lib/async-testing/test_loop.h>
#include <lib/sys/cpp/component_context.h>

#include "src/ui/scenic/lib/gfx/tests/mocks/mocks.h"
#include "src/ui/scenic/lib/scheduling/constant_frame_predictor.h"
#include "src/ui/scenic/lib/scheduling/default_frame_scheduler.h"

namespace scenic_impl {
namespace test {

void ScenicGfxTest::InitializeScenic(Scenic* scenic) {
  command_buffer_sequencer_ = std::make_unique<escher::impl::CommandBufferSequencer>();
  auto signaller =
      std::make_unique<gfx::test::ReleaseFenceSignallerForTest>(command_buffer_sequencer_.get());
  display_ = std::make_shared<display::Display>(
      /*id*/ 0, /* width */ 0, /* height */ 0);

  // TODO(SCN-421)): This frame scheduler is only needed for a single test in scenic_unittests.cc.
  // When this bug is fixed, that test will no longer depend on a GfxSystem, at which point, this
  // frame scheduler can be removed.
  frame_scheduler_ = std::make_shared<scheduling::DefaultFrameScheduler>(
      display_, std::make_unique<scheduling::ConstantFramePredictor>(zx::msec(5)),
      scenic_->inspect_node()->CreateChild("FrameScheduler"));

  auto context = sys::ComponentContext::Create();
  engine_ = std::make_unique<gfx::Engine>(context.get(), frame_scheduler_, std::move(signaller),
                                          escher::EscherWeakPtr());
  auto system = scenic->RegisterSystem<gfx::GfxSystem>(engine_.get(), escher::EscherWeakPtr(),
                                                       /* sysmem */ nullptr,
                                                       /* display_manager */ nullptr);
  scenic->SetInitialized();

  RunLoopUntilIdle();  // Finish initialization
}

void ScenicGfxTest::TearDown() {
  ScenicTest::TearDown();
  engine_.reset();
  frame_scheduler_.reset();
  display_.reset();
  command_buffer_sequencer_.reset();
}

}  // namespace test
}  // namespace scenic_impl
