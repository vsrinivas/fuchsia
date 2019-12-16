// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_TESTS_GFX_TEST_H_
#define SRC_UI_SCENIC_LIB_GFX_TESTS_GFX_TEST_H_

#include <lib/sys/cpp/testing/component_context_provider.h>

#include <memory>

#include "src/ui/scenic/lib/gfx/gfx_system.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/mocks.h"
#include "src/ui/scenic/lib/scenic/tests/scenic_test.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class GfxSystemTest : public scenic_impl::test::ScenicTest {
 public:
  GfxSystemTest() = default;
  ~GfxSystemTest() override = default;

  // ::testing::Test virtual method.
  void TearDown() override;

  GfxSystem* gfx_system() { return gfx_system_.get(); }
  Engine* engine() { return engine_.get(); }
  sys::testing::ComponentContextProvider& context_provider() { return context_provider_; }

 private:
  void InitializeScenic(Scenic* scenic) override;

  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<escher::impl::CommandBufferSequencer> command_buffer_sequencer_;
  std::shared_ptr<scheduling::FrameScheduler> frame_scheduler_;
  std::unique_ptr<Engine> engine_;

  GfxSystemWeakPtr gfx_system_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_TESTS_GFX_TEST_H_
