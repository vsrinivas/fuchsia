// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/tests/scenic_gfx_test.h"

#include <lib/async-testing/test_loop.h>

#include "garnet/lib/ui/gfx/displays/display.h"
#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/scenic/tests/mocks.h"

namespace scenic_impl {
namespace test {

void ScenicGfxTest::InitializeScenic(Scenic* scenic) {
  auto display_manager = std::make_unique<gfx::DisplayManager>();
  display_manager->SetDefaultDisplayForTests(
      std::make_unique<test::TestDisplay>(
          /*id*/ 0, /* width */ 0, /* height */ 0));
  command_buffer_sequencer_ =
      std::make_unique<escher::impl::CommandBufferSequencer>();
  scenic_->RegisterSystem<GfxSystemForTest>(std::move(display_manager),
                                            command_buffer_sequencer_.get());

  RunLoopUntilIdle();  // Finish initialization
}

void ScenicGfxTest::TearDown() {
  ScenicTest::TearDown();
  command_buffer_sequencer_.reset();
}

}  // namespace test
}  // namespace scenic_impl
