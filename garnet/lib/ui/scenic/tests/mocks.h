// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_TESTS_MOCKS_H_
#define GARNET_LIB_UI_SCENIC_TESTS_MOCKS_H_

#include "garnet/lib/ui/gfx/gfx_system.h"
#include "garnet/lib/ui/scenic/tests/dummy_system.h"
#include "src/ui/lib/escher/flib/release_fence_signaller.h"

namespace scenic_impl {
namespace test {

class ReleaseFenceSignallerForTest : public escher::ReleaseFenceSignaller {
 public:
  ReleaseFenceSignallerForTest(
      escher::impl::CommandBufferSequencer* command_buffer_sequencer);

  void AddCPUReleaseFence(zx::event fence) override;

  uint32_t num_calls_to_add_cpu_release_fence() {
    return num_calls_to_add_cpu_release_fence_;
  }

 private:
  uint32_t num_calls_to_add_cpu_release_fence_ = 0;
};

class EngineForTest : public gfx::Engine {
 public:
  EngineForTest(sys::ComponentContext* component_context,
                gfx::DisplayManager* display_manager,
                std::unique_ptr<escher::ReleaseFenceSignaller> release_signaler,
                escher::EscherWeakPtr escher = escher::EscherWeakPtr());
};

class GfxSystemForTest : public gfx::GfxSystem {
 public:
  static constexpr TypeId kTypeId = kGfx;

  explicit GfxSystemForTest(
      SystemContext context,
      std::unique_ptr<gfx::DisplayManager> display_manager,
      escher::impl::CommandBufferSequencer* command_buffer_sequencer)
      : GfxSystem(std::move(context), std::move(display_manager)),
        command_buffer_sequencer_(command_buffer_sequencer) {}

  gfx::Engine* engine() { return engine_.get(); }

 private:
  std::unique_ptr<gfx::Engine> InitializeEngine() override {
    return std::make_unique<EngineForTest>(
        context()->app_context(), display_manager_.get(),
        std::make_unique<ReleaseFenceSignallerForTest>(
            command_buffer_sequencer_));
  }

  std::unique_ptr<escher::Escher> InitializeEscher() override {
    return nullptr;
  }

  escher::impl::CommandBufferSequencer* command_buffer_sequencer_;
};

// Device-independent "display"; for testing only. Needed to ensure GfxSystem
// doesn't wait for a device-driven "display ready" signal.
class TestDisplay : public scenic_impl::gfx::Display {
 public:
  TestDisplay(uint64_t id, uint32_t width_px, uint32_t height_px)
      : Display(id, width_px, height_px) {}
  ~TestDisplay() = default;
  bool is_test_display() const override { return true; }
};

class MockSystemWithDelayedInitialization : public DummySystem {
 public:
  // Expose to tests.
  using System::SetToInitialized;

  explicit MockSystemWithDelayedInitialization(SystemContext context)
      : DummySystem(std::move(context), false) {}
};

}  // namespace test
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_SCENIC_TESTS_MOCKS_H_
