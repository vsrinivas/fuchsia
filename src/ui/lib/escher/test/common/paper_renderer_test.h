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

  // Tear down the created frame.
  void TeardownFrame();

  // Initialize the GPU uploader, and configure the renderer to begin
  // a frame.
  void BeginRenderingFrame();

  // Generate all commands (including from renderer and GPU uploader)
  // and emit them to the command buffer.
  void EndRenderingFrame();

  // Get current image pixels from the frame.
  std::vector<uint8_t> GetPixelData();

  PaperRenderer* renderer() const { return renderer_.get(); }
  BatchGpuUploader* gpu_uploader() const { return gpu_uploader_.get(); }
  const ReadbackTest::FrameData& frame_data() const { return frame_data_; }
  TexturePtr depth_buffer() const { return renderer_->depth_buffers_[0]; }

 private:
  PaperRendererPtr renderer_;

  // Frame environment variables.
  ReadbackTest::FrameData frame_data_;
  PaperScenePtr scene_;
  std::vector<Camera> cameras_;
  std::shared_ptr<BatchGpuUploader> gpu_uploader_;
};

}  // namespace test
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_TEST_COMMON_PAPER_RENDERER_TEST_H_
