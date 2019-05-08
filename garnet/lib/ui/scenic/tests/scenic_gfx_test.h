// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_TESTS_SCENIC_GFX_TEST_H_
#define GARNET_LIB_UI_SCENIC_TESTS_SCENIC_GFX_TEST_H_

#include "garnet/lib/ui/scenic/tests/scenic_test.h"
#include "src/ui/lib/escher/impl/command_buffer_sequencer.h"

namespace scenic_impl {
namespace test {

// Subclass of ScenicTest for tests requiring Scenic with a gfx system installed
class ScenicGfxTest : public ScenicTest {
 protected:
  void TearDown() override;
  void InitializeScenic(Scenic* scenic) override;

  std::unique_ptr<escher::impl::CommandBufferSequencer>
      command_buffer_sequencer_;
};

}  // namespace test
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_SCENIC_TESTS_SCENIC_GFX_TEST_H_
