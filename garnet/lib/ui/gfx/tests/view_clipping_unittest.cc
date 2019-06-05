// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <zircon/syscalls.h>

#include "garnet/lib/ui/gfx/engine/engine_renderer_visitor.h"
#include "garnet/lib/ui/gfx/resources/camera.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer.h"
#include "garnet/lib/ui/gfx/resources/nodes/shape_node.h"
#include "garnet/lib/ui/gfx/resources/renderers/renderer.h"
#include "garnet/lib/ui/gfx/resources/shapes/rounded_rectangle_shape.h"
#include "garnet/lib/ui/gfx/resources/view_holder.h"
#include "garnet/lib/ui/gfx/tests/vk_session_test.h"
#include "gtest/gtest.h"
#include "lib/ui/gfx/util/time.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "sdk/lib/ui/scenic/cpp/view_token_pair.h"
#include "src/ui/lib/escher/paper/paper_renderer.h"
#include "src/ui/lib/escher/paper/paper_scene.h"
#include "src/ui/lib/escher/test/gtest_escher.h"

namespace scenic_impl {
namespace gfx {
namespace test {

using namespace escher;

class ViewClippingTest : public VkSessionTest {
 public:
  // We need a rounded rect factory and a view linker which
  // the base VkSessionTest doesn't have.
  std::unique_ptr<SessionForTest> CreateSession() override {
    SessionContext session_context = CreateBarebonesSessionContext();
    auto vulkan_device = CreateVulkanDeviceQueues();
    escher_ = std::make_unique<Escher>(vulkan_device);
    release_fence_signaller_ = std::make_unique<ReleaseFenceSignaller>(
        escher_->command_buffer_sequencer());
    image_factory_ = std::make_unique<ImageFactoryAdapter>(
        escher_->gpu_allocator(), escher_->resource_recycler());

    session_context.vk_device = escher_->vk_device();
    session_context.escher = escher_.get();
    session_context.escher_resource_recycler = escher_->resource_recycler();
    session_context.escher_image_factory = image_factory_.get();
    session_context.release_fence_signaller = release_fence_signaller_.get();

    rounded_rect_factory_ =
        std::make_unique<escher::RoundedRectFactory>(escher_->GetWeakPtr());
    session_context.escher_rounded_rect_factory = rounded_rect_factory_.get();

    view_linker_ = std::make_unique<ViewLinker>();
    session_context.view_linker = view_linker_.get();

    OnSessionContextCreated(&session_context);
    return std::make_unique<SessionForTest>(1, std::move(session_context), this,
                                            error_reporter());
  }

 private:
  std::unique_ptr<ViewLinker> view_linker_;
  std::unique_ptr<escher::RoundedRectFactory> rounded_rect_factory_;
};

static constexpr float kNear = 1.f;
static constexpr float kFar = -200.f;

static constexpr float kWidth = 1024;
static constexpr float kHeight = 768;

// This first unit test checks to see if a view holder
// is properly having its bounds set by the
// "SetViewPropertiesCmd" and if the correct clipping
// planes are being generated as a result.
#if SCENIC_ENFORCE_VIEW_BOUND_CLIPPING
VK_TEST_F(ViewClippingTest, ClipSettingTest) {
#else
VK_TEST_F(ViewClippingTest, DISABLED_ClipSettingTest) {
#endif
  uint32_t scene_id = 5;
  uint32_t view_id = 15;
  uint32_t view_holder_id = 30;

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  Apply(scenic::NewCreateSceneCmd(scene_id));
  Apply(scenic::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "MyViewHolder"));
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "MyView"));

  ViewHolder* view_holder = FindResource<ViewHolder>(view_holder_id).get();
  EXPECT_TRUE(view_holder);

  // Try a bunch of different bounding box configurations to make sure that
  // they all work.
  for (int32_t i = -10; i < 10; i++) {
    for (int32_t j = -10; j < 10; j++) {
      for (int32_t k = -10; k < 10; k++) {
        for (int32_t m = 1; m < 10; m++) {
          const float bbox_min[3] = {float(i), float(j), float(k)};
          const float bbox_max[3] = {float(i + m), float(j + m), float(k + m)};
          const float inset_min[3] = {0, 0, 0};
          const float inset_max[3] = {0, 0, 0};

          BoundingBox bbox(vec3(i, j, k), vec3(i + m, j + m, k + m));

          Apply(scenic::NewSetViewPropertiesCmd(
              view_holder_id, bbox_min, bbox_max, inset_min, inset_max));
          const std::vector<plane3>& clip_planes = view_holder->clip_planes();

          std::vector<plane3> test_planes = bbox.CreatePlanes();

          EXPECT_EQ(clip_planes.size(), test_planes.size());

          for (uint32_t p = 0; p < clip_planes.size(); p++) {
            plane3 test_plane = test_planes[p];
            plane3 view_plane = clip_planes[p];
            EXPECT_EQ(test_plane.dir(), view_plane.dir());
            EXPECT_EQ(test_plane.dist(), view_plane.dist());
          }
        }
      }
    }
  }
}

// This test is used to check that meshes get clipped properly
// by their view holder's clip planes when the EngineRendererVisitor
// traverses the scene.
#if SCENIC_ENFORCE_VIEW_BOUND_CLIPPING
VK_TEST_F(ViewClippingTest, SceneTraversal) {
#else
VK_TEST_F(ViewClippingTest, DISABLED_SceneTraversal) {
#endif
  auto escher = escher::test::GetEscher()->GetWeakPtr();

  uint32_t scene_id = 5;
  uint32_t view_id = 15;
  uint32_t view_holder_id = 30;
  uint32_t shape_node_id = 50;
  uint32_t material_id = 60;
  uint32_t rect_id = 70;

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const float bbox_min[3] = {0, 0, kFar};
  const float bbox_max[3] = {kWidth, kHeight, kNear};
  const float inset_min[3] = {0, 0, 0};
  const float inset_max[3] = {0, 0, 0};

  Apply(scenic::NewCreateSceneCmd(scene_id));

  Apply(scenic::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "MyViewHolder"));

  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "MyView"));

  Apply(scenic::NewSetViewPropertiesCmd(view_holder_id, bbox_min, bbox_max,
                                        inset_min, inset_max));

  Apply(scenic::NewCreateShapeNodeCmd(shape_node_id));

  // Set shape to shape node.
  EXPECT_TRUE(Apply(scenic::NewCreateRoundedRectangleCmd(rect_id, 30.f, 40.f,
                                                         2.f, 4.f, 6.f, 8.f)));

  Apply(scenic::NewSetShapeCmd(shape_node_id, rect_id));

  // Set material to shape node.
  Apply(scenic::NewCreateMaterialCmd(material_id));
  Apply(scenic::NewSetColorCmd(material_id, 255, 255, 255, 255));
  Apply(scenic::NewSetMaterialCmd(shape_node_id, material_id));

  Apply(scenic::NewAddChildCmd(scene_id, view_holder_id));
  Apply(scenic::NewAddChildCmd(view_id, shape_node_id));

  ScenePtr scene = FindResource<Scene>(scene_id);

  // Make default paper scene.
  PaperScenePtr paper_scene = fxl::MakeRefCounted<PaperScene>();
  paper_scene->bounding_box =
      BoundingBox(vec3(0.f, 0.f, kFar), vec3(kWidth, kHeight, kNear));

  ViewingVolume volume(paper_scene->bounding_box);

  // Make escher camera.
  escher::Camera camera = escher::Camera::NewOrtho(volume);

  // Make paper renderer.
  PaperRendererPtr paper_renderer = PaperRenderer::New(escher);

  // Make frame.
  FramePtr frame = escher->NewFrame("ViewClippingFrame", 0);

  // Make output image.
  ImageInfo info;
  info.format = vk::Format::eB8G8R8A8Srgb;
  info.width = kWidth;
  info.height = kHeight;
  info.usage = vk::ImageUsageFlagBits::eColorAttachment;
  auto image_cache = escher->image_cache();
  auto output_image = image_cache->NewImage(info);

  PaperDrawCallFactory* draw_call_factory = paper_renderer->draw_call_factory();
  draw_call_factory->set_track_cache_entries(true);

  paper_renderer->BeginFrame(frame, paper_scene, {camera}, output_image);

  BatchGpuUploader gpu_uploader(escher, frame->frame_number());
  EngineRendererVisitor visitor(paper_renderer.get(), &gpu_uploader);
  visitor.Visit(scene.get());

  // Get the cache entries from the PaperDrawcallFactory.
  const std::vector<PaperShapeCacheEntry>& cache_entries =
      draw_call_factory->tracked_cache_entries();

  EXPECT_TRUE(cache_entries.size() == 1);

  const PaperShapeCacheEntry& entry = cache_entries[0];

  // Now we're going to manually create a cache entry using the same
  // rounded rectangle and we're going to see if it matches the entry
  // we got from going through the whole pipeline with the view
  // properties set.
  ShapeNode* shape_node = FindResource<ShapeNode>(shape_node_id).get();
  EXPECT_TRUE(shape_node);

  const ShapePtr& shape = shape_node->shape();
  EXPECT_TRUE(shape);
  EXPECT_TRUE(shape->IsKindOf<RoundedRectangleShape>());

  auto rect = static_cast<RoundedRectangleShape*>(shape.get());

  auto spec = rect->spec();

  // These are the planes that the above view holder properties should generate.
  std::vector<plane3> planes;
  planes.push_back(plane3(vec3(1, 0, 0), 0));
  planes.push_back(plane3(vec3(0, 1, 0), 0));
  planes.push_back(plane3(vec3(0, 0, 1), -200));
  planes.push_back(plane3(vec3(-1, 0, 0), -1024));
  planes.push_back(plane3(vec3(0, -1, 0), -768));
  planes.push_back(plane3(vec3(0, 0, -1), -1));

  PaperShapeCache cache(escher, PaperRendererConfig());
  cache.BeginFrame(&gpu_uploader, 0);

  const PaperShapeCacheEntry& entry2 =
      cache.GetRoundedRectMesh(spec, planes.data(), 6);

  // Cache entries should be identitcal.
  EXPECT_EQ(entry.mesh->num_vertices(), entry2.mesh->num_vertices());
  EXPECT_EQ(entry.num_indices, entry2.num_indices);
  EXPECT_EQ(entry.num_shadow_volume_indices, entry2.num_shadow_volume_indices);

  gpu_uploader.Submit();

  // End frame
  paper_renderer->EndFrame();
  cache.EndFrame();

  auto frame_done_semaphore = Semaphore::New(escher->vk_device());
  frame->EndFrame(frame_done_semaphore, nullptr);

  output_image = nullptr;

  escher->vk_device().waitIdle();
  escher->Cleanup();
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
