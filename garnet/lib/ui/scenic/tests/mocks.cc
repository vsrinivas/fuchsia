// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/tests/mocks.h"

#include "garnet/lib/ui/gfx/engine/default_frame_scheduler.h"

namespace scenic_impl {
namespace test {

ReleaseFenceSignallerForTest::ReleaseFenceSignallerForTest(
    escher::impl::CommandBufferSequencer* command_buffer_sequencer)
    : ReleaseFenceSignaller(command_buffer_sequencer) {}

void ReleaseFenceSignallerForTest::AddCPUReleaseFence(zx::event fence) {
  ++num_calls_to_add_cpu_release_fence_;
  // Signal immediately for testing purposes.
  fence.signal(0u, escher::kFenceSignalled);
}

EngineForTest::EngineForTest(
    sys::ComponentContext* component_context,
    gfx::DisplayManager* display_manager,
    std::unique_ptr<escher::ReleaseFenceSignaller> release_signaler,
    escher::EscherWeakPtr escher)
    : gfx::Engine(component_context,
                  std::make_unique<gfx::DefaultFrameScheduler>(
                      display_manager->default_display()),
                  display_manager, std::move(release_signaler),
                  std::make_unique<gfx::SessionManager>(), std::move(escher)) {}

}  // namespace test
}  // namespace scenic_impl
