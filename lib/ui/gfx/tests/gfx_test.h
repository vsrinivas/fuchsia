// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_GFX_TEST_H_
#define GARNET_LIB_UI_GFX_TESTS_GFX_TEST_H_

#include "garnet/lib/ui/gfx/gfx_system.h"
#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "garnet/lib/ui/scenic/tests/scenic_test.h"

namespace scenic {
namespace gfx {
namespace test {

class GfxSystemForTest : public GfxSystem {
 public:
  static constexpr TypeId kTypeId = kGfx;

  explicit GfxSystemForTest(
      SystemContext context,
      escher::impl::CommandBufferSequencer* command_buffer_sequencer)
      : GfxSystem(std::move(context)),
        command_buffer_sequencer_(command_buffer_sequencer) {}

  Engine* engine() { return engine_.get(); }

 private:
  std::unique_ptr<Engine> InitializeEngine() override {
    return std::make_unique<EngineForTest>(
        &display_manager_, std::make_unique<ReleaseFenceSignallerForTest>(
                               command_buffer_sequencer_));
  }

  std::unique_ptr<escher::Escher> InitializeEscher() override {
    return nullptr;
  }

  escher::impl::CommandBufferSequencer* command_buffer_sequencer_;
  DisplayManager display_manager_;
};

class GfxSystemTest : public ::scenic::test::ScenicTest {
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
        command_buffer_sequencer_.get());
  }
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_TESTS_GFX_TEST_H_
