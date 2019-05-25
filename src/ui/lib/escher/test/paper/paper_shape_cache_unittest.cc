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

VK_TEST(PaperShapeCache, TestCaching) {
  EscherWeakPtr escher = test::GetEscher()->GetWeakPtr();

  plane3 planes[2] = {plane3(vec3(1, 0, 0), -1.f), plane3(vec3(0, 1, 0), -1.f)};

  PaperShapeCache cache(escher, PaperRendererConfig());

  // Request two different rectangles.
  uint64_t frame_number = 1;
  {
    auto frame = escher->NewFrame("PaperShapeCache unit-test", frame_number);

    auto uploader = BatchGpuUploader::New(escher);

    cache.BeginFrame(uploader.get(), frame_number);

    auto& entry0 = cache.GetRectMesh(3, 3, &planes[0], 1);
    EXPECT_NE(entry0.mesh, MeshPtr());
    EXPECT_EQ(cache.size(), 1U);

    auto& entry0a = cache.GetRectMesh(3, 3, &planes[0], 1);
    EXPECT_EQ(entry0.mesh, entry0a.mesh);
    EXPECT_EQ(cache.size(), 1U);

    auto& entry1 = cache.GetRectMesh(3, 3, &planes[1], 1);
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

    auto& entry0 = cache.GetRectMesh(3, 3, &planes[0], 1);
    EXPECT_NE(entry0.mesh, MeshPtr());

    auto& entry2 = cache.GetRectMesh(3, 3, planes, 2);
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

VK_TEST(PaperShapeCache, WaitSemaphores) {
  auto escher = test::GetEscher()->GetWeakPtr();

  PaperShapeCache cache(escher, PaperRendererConfig());

  auto texture = escher->NewTexture(
      vk::Format::eR8G8B8A8Unorm, 100, 100, 1, vk::ImageUsageFlagBits::eSampled,
      vk::Filter::eLinear, vk::ImageAspectFlagBits::eColor);

  uint64_t kFrameNumber = 1;

  // Convenient wrapper around PaperRenderFuncs::NewMeshData().
  auto NewMeshData = [&](PaperShapeCacheEntry& entry, const FramePtr& frame) {
    return PaperRenderFuncs::NewMeshData(frame, entry.mesh.get(), texture,
                                         entry.num_indices,
                                         entry.num_shadow_volume_indices);
  };

  // Count the number of wait semaphores attached to all the mesh's buffers.
  auto CountWaitSemaphores = [&](PaperShapeCacheEntry& entry) {
    std::set<Buffer*> buffer_set;
    buffer_set.insert(entry.mesh->index_buffer().get());
    for (auto& ab : entry.mesh->attribute_buffers()) {
      if (ab.buffer) {
        buffer_set.insert(ab.buffer.get());
      }
    }
    size_t count = 0;
    for (auto buf : buffer_set) {
      if (buf->HasWaitSemaphore()) {
        ++count;
      }
    }
    return count;
  };

  {
    auto frame = escher->NewFrame("PaperRenderer unit test", kFrameNumber);
    auto cmd_buf = frame->command_buffer();

    auto uploader = BatchGpuUploader::New(escher);

    cache.BeginFrame(uploader.get(), kFrameNumber);

    auto entry1 = cache.GetRoundedRectMesh(
        RoundedRectSpec(100, 100, 5, 5, 5, 5), nullptr, 0);
    EXPECT_EQ(1U, cache.cache_miss_count());
    EXPECT_EQ(0U, cache.cache_hit_count());

    size_t entry_sema_count = CountWaitSemaphores(entry1);
    EXPECT_GT(entry_sema_count, 0U);

    // Creating a MeshData should strip the mesh of its wait semaphores,
    // and add them to the current Frame.
    size_t frame_sema_count = cmd_buf->NumWaitSemaphores();
    EXPECT_EQ(0U, frame_sema_count);
    PaperRenderFuncs::MeshData* mesh_data = NewMeshData(entry1, frame);
    frame_sema_count = cmd_buf->NumWaitSemaphores();
    EXPECT_EQ(frame_sema_count, entry_sema_count);
    entry_sema_count = CountWaitSemaphores(entry1);
    EXPECT_EQ(0U, entry_sema_count);

    // Obtain the same cache entry.
    auto entry2 = cache.GetRoundedRectMesh(
        RoundedRectSpec(100, 100, 5, 5, 5, 5), nullptr, 0);

    EXPECT_EQ(1U, cache.cache_miss_count());
    EXPECT_EQ(1U, cache.cache_hit_count());
    EXPECT_EQ(entry1.mesh, entry2.mesh);

    // Getting the same entry does not upload it again, so there are no wait
    // semaphores.
    EXPECT_EQ(0U, CountWaitSemaphores(entry2));

    uploader->Submit();
    cache.EndFrame();

    frame->EndFrame(SemaphorePtr(), []() {});
  }

  escher->vk_device().waitIdle();
  ASSERT_TRUE(escher->Cleanup());
}

}  // namespace
