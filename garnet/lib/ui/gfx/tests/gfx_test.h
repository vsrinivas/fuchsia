// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_GFX_TEST_H_
#define GARNET_LIB_UI_GFX_TESTS_GFX_TEST_H_

#include <memory>

#include "garnet/lib/ui/gfx/gfx_system.h"
#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "garnet/lib/ui/scenic/tests/scenic_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class GfxSystemTest : public scenic_impl::test::ScenicTest {
 public:
  // ::testing::Test virtual method.
  void TearDown() override;

  GfxSystem* gfx_system() { return gfx_system_.get(); }

 private:
  void InitializeScenic(Scenic* scenic) override;

  std::unique_ptr<escher::impl::CommandBufferSequencer> command_buffer_sequencer_;
  std::unique_ptr<Display> display_;
  std::shared_ptr<FrameScheduler> frame_scheduler_;
  std::unique_ptr<Engine> engine_;

  GfxSystemWeakPtr gfx_system_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_TESTS_GFX_TEST_H_
