// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/paper/paper_shape_cache.h"

#include "src/ui/lib/escher/paper/paper_render_funcs.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/shape/mesh.h"
#include "src/ui/lib/escher/shape/rounded_rect.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace {
using namespace escher;

using PaperShapeCacheTest = escher::test::TestWithVkValidationLayer;

// Make sure that the PaperShapeCache properly creates
// a box mesh.
VK_TEST_F(PaperShapeCacheTest, TestBoundingBox) {
  EscherWeakPtr escher = test::GetEscher()->GetWeakPtr();
  PaperShapeCache cache(escher, PaperRendererConfig());

  // Request a bounding box.
  uint64_t frame_number = 1;
  {
    auto frame = escher->NewFrame("PaperShapeCache unit-test", frame_number);

    auto uploader = BatchGpuUploader::New(escher);

    cache.BeginFrame(uploader.get(), frame_number);

    const auto& entry = cache.GetBoxMesh(nullptr, 0);
    EXPECT_NE(entry.mesh, MeshPtr());
    EXPECT_EQ(cache.size(), 1U);

    // A box has 36 indices and 8 vertices.
    EXPECT_EQ(entry.num_indices, 36U);
    EXPECT_EQ(entry.mesh->num_vertices(), 8U);

    uploader->Submit();
    cache.EndFrame();

    frame->EndFrame(SemaphorePtr(), []() {});
  }

  escher->vk_device().waitIdle();
  ASSERT_TRUE(escher->Cleanup());
}

VK_TEST_F(PaperShapeCacheTest, TestCaching) {
  EscherWeakPtr escher = test::GetEscher()->GetWeakPtr();

  plane3 planes[2] = {plane3(vec3(1, 0, 0), -1.f), plane3(vec3(0, 1, 0), -1.f)};

  PaperShapeCache cache(escher, PaperRendererConfig());

  // Request two different rectangles.
  uint64_t frame_number = 1;
  {
    auto frame = escher->NewFrame("PaperShapeCache unit-test", frame_number);

    auto uploader = BatchGpuUploader::New(escher);

    cache.BeginFrame(uploader.get(), frame_number);

    const auto& entry0 = cache.GetRectMesh(3, 3, &planes[0], 1);
    EXPECT_NE(entry0.mesh, MeshPtr());
    EXPECT_EQ(cache.size(), 1U);

    const auto& entry0a = cache.GetRectMesh(3, 3, &planes[0], 1);
    EXPECT_EQ(entry0.mesh, entry0a.mesh);
    EXPECT_EQ(cache.size(), 1U);

    const auto& entry1 = cache.GetRectMesh(3, 3, &planes[1], 1);
    EXPECT_NE(entry1.mesh, MeshPtr());
    EXPECT_NE(entry1.mesh, entry0.mesh);

    uploader->Submit();
    cache.EndFrame();

    frame->EndFrame(SemaphorePtr(), []() {});

    EXPECT_EQ(cache.size(), 2U);
  }

  // Request one of the two rectangles from the previous frame, and a different
  // rectangle.
  frame_number = 2;
  {
    auto frame = escher->NewFrame("PaperShapeCache unit-test", frame_number);

    auto uploader = BatchGpuUploader::New(escher);

    cache.BeginFrame(uploader.get(), frame_number);

    const auto& entry0 = cache.GetRectMesh(3, 3, &planes[0], 1);
    EXPECT_NE(entry0.mesh, MeshPtr());

    const auto& entry2 = cache.GetRectMesh(3, 3, planes, 2);
    EXPECT_NE(entry2.mesh, MeshPtr());
    EXPECT_NE(entry2.mesh, entry0.mesh);

    uploader->Submit();
    cache.EndFrame();

    frame->EndFrame(SemaphorePtr(), []() {});

    EXPECT_EQ(cache.size(), 3U);
  }

  // Request no rectangles.  All three should still be cached.
  frame_number = 3;
  {
    auto frame = escher->NewFrame("PaperShapeCache unit-test", frame_number);

    auto uploader = BatchGpuUploader::New(escher);

    cache.BeginFrame(uploader.get(), frame_number);

    uploader->Submit();
    cache.EndFrame();

    frame->EndFrame(SemaphorePtr(), []() {});

    EXPECT_EQ(cache.size(), 3U);
  }

  // Request no rectangles.  Only two should remain cached.
  frame_number = 4;
  EXPECT_EQ(frame_number, 1 + PaperShapeCache::kNumFramesBeforeEviction);
  {
    auto frame = escher->NewFrame("PaperShapeCache unit-test", frame_number);

    auto uploader = BatchGpuUploader::New(escher);

    cache.BeginFrame(uploader.get(), frame_number);

    uploader->Submit();
    cache.EndFrame();

    frame->EndFrame(SemaphorePtr(), []() {});

    EXPECT_EQ(cache.size(), 2U);
  }

  // Request no rectangles.  None should remain cached.
  frame_number = 5;
  EXPECT_EQ(frame_number, 2 + PaperShapeCache::kNumFramesBeforeEviction);
  {
    auto frame = escher->NewFrame("PaperShapeCache unit-test", frame_number);

    auto uploader = BatchGpuUploader::New(escher);

    cache.BeginFrame(uploader.get(), frame_number);

    uploader->Submit();
    cache.EndFrame();

    frame->EndFrame(SemaphorePtr(), []() {});

    EXPECT_EQ(cache.size(), 0U);
  }
}

// Test that generated/cached mesh has the expected number of vertices when stencil shadows are
// are enabled.  This is necessary because be generate different geometry in this case than in other
// shadow modes.
VK_TEST_F(PaperShapeCacheTest, TestShadowCaching) {
  EscherWeakPtr escher = test::GetEscher()->GetWeakPtr();

  PaperRendererConfig config;

  PaperShapeCache cache(escher, config);

  constexpr float kRectWidth = 300.f;
  constexpr float kRectHeight = 200.f;

  // A rectangle has 6 indices and 4 vertices.
  constexpr uint32_t kRectIndexCount = 6U;
  constexpr uint32_t kRectVertexCount = 4U;

  // When rendering a rectangle for shadow volumes, we render 6 rectangles: the original rectangle
  // plus one for each face of the shadow frustum (4 side faces and one bottom face).
  constexpr uint32_t kShadowRectIndexCount = kRectIndexCount * 6U;
  // The shadow volume rect has twice as many vertices: the additional vertices form the rectangle
  // at the bottom face of the shadow frustum.
  constexpr uint32_t kShadowRectVertexCount = kRectVertexCount * 2U;

  uint64_t frame_number = 1;
  {
    auto frame = escher->NewFrame("PaperShapeCache unit-test", frame_number);

    auto uploader = BatchGpuUploader::New(escher);

    cache.BeginFrame(uploader.get(), frame_number);

    const auto& entry = cache.GetRectMesh(kRectWidth, kRectHeight, nullptr, 0);
    EXPECT_NE(entry.mesh, MeshPtr());
    EXPECT_EQ(cache.size(), 1U);

    EXPECT_EQ(entry.num_indices, kRectIndexCount);
    EXPECT_EQ(entry.num_shadow_volume_indices, 0U);
    EXPECT_EQ(entry.mesh->num_vertices(), kRectVertexCount);

    uploader->Submit();
    cache.EndFrame();

    frame->EndFrame(SemaphorePtr(), []() {});
  }

  config.shadow_type = PaperRendererShadowType::kShadowVolume;
  cache.SetConfig(config);
  frame_number = 2;
  {
    auto frame = escher->NewFrame("PaperShapeCache unit-test", frame_number);

    auto uploader = BatchGpuUploader::New(escher);

    cache.BeginFrame(uploader.get(), frame_number);

    const auto& entry = cache.GetRectMesh(kRectWidth, kRectHeight, nullptr, 0);
    EXPECT_NE(entry.mesh, MeshPtr());
    EXPECT_EQ(cache.size(), 1U);

    EXPECT_EQ(entry.num_indices, kRectIndexCount);
    EXPECT_EQ(entry.num_shadow_volume_indices, kShadowRectIndexCount);
    EXPECT_EQ(entry.mesh->num_vertices(), kShadowRectVertexCount);

    uploader->Submit();
    cache.EndFrame();

    frame->EndFrame(SemaphorePtr(), []() {});
  }

  escher->vk_device().waitIdle();
  ASSERT_TRUE(escher->Cleanup());
}

}  // namespace
