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
  void SetUp() override {
    command_buffer_sequencer_ = std::make_unique<escher::impl::CommandBufferSequencer>();
    ScenicTest::SetUp();
  }

  // ::testing::Test virtual method.
  void TearDown() override {
    ScenicTest::TearDown();
    command_buffer_sequencer_.reset();
  }

  GfxSystemForTest* gfx_system() { return gfx_system_; }

 private:
  std::unique_ptr<escher::impl::CommandBufferSequencer> command_buffer_sequencer_;

  GfxSystemForTest* gfx_system_ = nullptr;

  void InitializeScenic(Scenic* scenic) override {
    auto display_manager = std::make_unique<gfx::DisplayManager>();
    display_manager->SetDefaultDisplayForTests(std::make_unique<TestDisplay>(
        /*id*/ 0, /* width */ 0, /* height */ 0));
    command_buffer_sequencer_ = std::make_unique<escher::impl::CommandBufferSequencer>();
    gfx_system_ = scenic->RegisterSystem<GfxSystemForTest>(std::move(display_manager),
                                                           command_buffer_sequencer_.get());
  }
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_TESTS_GFX_TEST_H_
