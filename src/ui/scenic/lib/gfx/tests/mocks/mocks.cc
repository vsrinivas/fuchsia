// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/tests/mocks/mocks.h"

#include "src/ui/scenic/lib/scenic/command_dispatcher.h"

namespace scenic_impl {
namespace gfx {
namespace test {

ReleaseFenceSignallerForTest::ReleaseFenceSignallerForTest(
    escher::impl::CommandBufferSequencer* command_buffer_sequencer)
    : ReleaseFenceSignaller(command_buffer_sequencer) {}

void ReleaseFenceSignallerForTest::AddCPUReleaseFence(zx::event fence) {
  // Signal immediately for testing purposes.
  fence.signal(0u, escher::kFenceSignalled);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
