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

class GfxSystemForTest : public GfxSystem {
 public:
  static constexpr TypeId kTypeId = kGfx;

  explicit GfxSystemForTest(
      SystemContext context, std::unique_ptr<DisplayManager> display_manager,
      escher::impl::CommandBufferSequencer* command_buffer_sequencer)
      : GfxSystem(std::move(context), std::move(display_manager)),
        command_buffer_sequencer_(command_buffer_sequencer) {}

  Engine* engine() { return engine_.get(); }

 private:
  std::unique_ptr<Engine> InitializeEngine() override {
    return std::make_unique<EngineForTest>(
        display_manager_.get(), std::make_unique<ReleaseFenceSignallerForTest>(
                                    command_buffer_sequencer_));
  }

  std::unique_ptr<escher::Escher> InitializeEscher() override {
    return nullptr;
  }

  escher::impl::CommandBufferSequencer* command_buffer_sequencer_;
};

class GfxSystemTest : public ::scenic_impl::test::ScenicTest {
 public:
  // ::testing::Test virtual method.
  void SetUp() override {
    command_buffer_sequencer_ =
        std::make_unique<escher::impl::CommandBufferSequencer>();
    ScenicTest::SetUp();
  }

  // ::testing::Test virtual method.
  void TearDown() override {
    ScenicTest::TearDown();
    command_buffer_sequencer_.reset();
  }

  GfxSystemForTest* gfx_system() { return gfx_system_; }

 private:
  std::unique_ptr<escher::impl::CommandBufferSequencer>
      command_buffer_sequencer_;

  GfxSystemForTest* gfx_system_;

  void InitializeScenic(Scenic* scenic) override {
    gfx_system_ = scenic->RegisterSystem<GfxSystemForTest>(
        std::make_unique<DisplayManager>(), command_buffer_sequencer_.get());
  }
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_TESTS_GFX_TEST_H_
