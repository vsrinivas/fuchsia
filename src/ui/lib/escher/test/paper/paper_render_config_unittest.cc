// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <type_traits>

#include "src/ui/lib/escher/paper/paper_renderer.h"
#include "src/ui/lib/escher/paper/paper_renderer_config.h"
#include "src/ui/lib/escher/test/gtest_escher.h"

namespace {

using namespace escher;

using PaperRendererConfigTest = escher::test::TestWithVkValidationLayer;

VK_TEST_F(PaperRendererConfigTest, TestInvalidSampleCount) {
  EscherWeakPtr escher = test::GetEscher()->GetWeakPtr();
  PaperRendererPtr renderer = PaperRenderer::New(escher, PaperRendererConfig());
  auto old_config = renderer->config();
  auto new_config = renderer->config();

  const std::vector<size_t> kSampleCountCandidates = {1, 2, 4};
  auto it = std::find_if(kSampleCountCandidates.begin(), kSampleCountCandidates.end(),
                         [escher](const size_t sample_count) {
                           const auto& msaa_sample_counts =
                               escher->device()->caps().msaa_sample_counts;
                           return msaa_sample_counts.find(sample_count) == msaa_sample_counts.end();
                         });
  if (it == kSampleCountCandidates.end()) {
    FXL_LOG(INFO) << "Cannot find a sample count not supported by the device. Test terminated.";
  } else {
    new_config.msaa_sample_count = *it;
    FXL_LOG(INFO) << "Setting the sample count to a value not upported by the device. "
                     "Error messages are expected.";
    renderer->SetConfig(new_config);

    // SetConfig should fail and the renderer config should not change.
    EXPECT_TRUE(renderer->config().msaa_sample_count != new_config.msaa_sample_count);
    EXPECT_TRUE(renderer->config().msaa_sample_count == old_config.msaa_sample_count);
  }
}

VK_TEST_F(PaperRendererConfigTest, TestInvalidDepthStencilFormat) {
  EscherWeakPtr escher = test::GetEscher()->GetWeakPtr();
  PaperRendererPtr renderer = PaperRenderer::New(escher, PaperRendererConfig());
  auto old_config = renderer->config();
  auto new_config = renderer->config();

  new_config.depth_stencil_format = vk::Format::eUndefined;
  FXL_LOG(INFO) << "Setting the depth stencil format to a format not supported by the device. "
                   "Error messages are expected.";
  renderer->SetConfig(new_config);

  // SetConfig should fail and the renderer config should not change.
  EXPECT_TRUE(renderer->config().depth_stencil_format != new_config.depth_stencil_format);
  EXPECT_TRUE(renderer->config().depth_stencil_format == old_config.depth_stencil_format);
}

}  // namespace
