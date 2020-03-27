// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TEST_COMMON_PAPER_RENDERER_TEST_H_
#define SRC_UI_LIB_ESCHER_TEST_COMMON_PAPER_RENDERER_TEST_H_

#include "src/ui/lib/escher/debug/debug_rects.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/paper/paper_renderer.h"
#include "src/ui/lib/escher/test/common/readback_test.h"

namespace escher {
namespace test {

// Extends ReadbackTest by providing a ready-to-use DebugFont instance.
class PaperRendererTest : public ReadbackTest {
 protected:
  // |ReadbackTest|
  void SetUp() override;

  // |ReadbackTest|
  void TearDown() override;

  // Sets up the environment including the frame, scene, and cameras.
  void SetupFrame();

  // Get current image data from the frame.
  std::vector<uint8_t> GetFrameData();

 public:
  TexturePtr depth_buffer() { return ren->depth_buffers_[0]; }

  // TODO(48928): Rename these member variables and make them private,
  // in tests inheriting from this fixture we should use getters instead.
  escher::PaperRendererPtr ren;

  // Frame environment variables.
  ReadbackTest::FrameData fd;
  escher::PaperScenePtr scene;
  std::vector<Camera> cameras;
};

}  // namespace test
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_TEST_COMMON_PAPER_RENDERER_TEST_H_
