// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <zircon/syscalls.h>

#include "gtest/gtest.h"
#include "sdk/lib/ui/scenic/cpp/view_token_pair.h"
#include "src/ui/lib/escher/paper/paper_renderer.h"
#include "src/ui/lib/escher/paper/paper_scene.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/vk/image_layout_updater.h"
#include "src/ui/scenic/lib/gfx/engine/engine_renderer_visitor.h"
#include "src/ui/scenic/lib/gfx/resources/camera.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/resources/renderers/renderer.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/rounded_rectangle_shape.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"
#include "src/ui/scenic/lib/gfx/util/time.h"

namespace scenic_impl {
namespace gfx {
namespace test {

using namespace escher;

class ViewClippingTest : public VkSessionTest {
 public:
  // We need a rounded rect factory and a view linker which
  // the base VkSessionTest doesn't have.
  void TearDown() override {
    VkSessionTest::TearDown();
    view_linker_.reset();
  }

  // We need a view linker which the base VkSessionTest doesn't have.
  SessionContext CreateSessionContext() override {
    auto session_context = VkSessionTest::CreateSessionContext();

    FXL_DCHECK(!view_linker_);

    view_linker_ = std::make_unique<ViewLinker>();
    session_context.view_linker = view_linker_.get();

    return session_context;
  }

 private:
  std::unique_ptr<ViewLinker> view_linker_;
};

static constexpr float kNear = 1.f;
static constexpr float kFar = -200.f;

static constexpr float kWidth = 1024;
static constexpr float kHeight = 768;

// Simple unit test to check that view bound colors
// for debug wireframe rendering are being set properly.
VK_TEST_F(ViewClippingTest, SetBoundsRenderingTest) {
  uint32_t scene_id = 5;
  uint32_t view_id = 15;
  uint32_t view_holder_id = 30;

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  Apply(scenic::NewCreateSceneCmd(scene_id));
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token), "ViewHolder"));
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "View"));

  Apply(scenic::NewSetViewHolderBoundsColorCmd(view_holder_id, 255, 0, 255));

  ViewHolder* view_holder = FindResource<ViewHolder>(view_holder_id).get();
  EXPECT_TRUE(view_holder);

  glm::vec4 color = view_holder->bounds_color() * 255.f;
  EXPECT_EQ(color, glm::vec4(255, 0, 255, 255));
}

// This first unit test checks to see if a view holder
// is properly having its bounds set by the
// "SetViewPropertiesCmd" and if the correct clipping
// planes are being generated as a result.
VK_TEST_F(ViewClippingTest, ClipSettingTest) {
  uint32_t scene_id = 5;
  uint32_t view_id = 15;
  uint32_t view_holder_id = 30;

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  Apply(scenic::NewCreateSceneCmd(scene_id));
  Apply(
      scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token), "MyViewHolder"));
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "MyView"));

  ViewHolder* view_holder = FindResource<ViewHolder>(view_holder_id).get();
  EXPECT_TRUE(view_holder);

  // Try a bunch of different bounding box configurations to make sure that
  // they all work.
  for (int32_t i = -10; i < 10; i++) {
    for (int32_t j = -10; j < 10; j++) {
      for (int32_t k = -10; k < 10; k++) {
        for (int32_t m = 1; m < 10; m++) {
          const std::array<float, 3> bbox_min = {float(i), float(j), float(k)};
          const std::array<float, 3> bbox_max = {float(i + m), float(j + m), float(k + m)};
          const std::array<float, 3> inset_min = {0, 0, 0};
          const std::array<float, 3> inset_max = {0, 0, 0};

          BoundingBox bbox(vec3(i, j, k), vec3(i + m, j + m, k + m));

          Apply(scenic::NewSetViewPropertiesCmd(view_holder_id, bbox_min, bbox_max, inset_min,
                                                inset_max));
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

// This first unit test checks to see if a view holder
// is properly having its bounds set by the
// "SetViewPropertiesCmd" and if the correct clipping
// planes are being generated as a result.
VK_TEST_F(ViewClippingTest, InsetsTest) {
  uint32_t scene_id = 5;
  uint32_t view_id = 15;
  uint32_t view_holder_id = 30;

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  Apply(scenic::NewCreateSceneCmd(scene_id));
  Apply(scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token), "ViewHolder"));
  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "View"));

  ViewHolder* view_holder = FindResource<ViewHolder>(view_holder_id).get();
  EXPECT_TRUE(view_holder);

  // Set view bounding box properties.
  const std::array<float, 3> bbox_min = {0, 0, -100};
  const std::array<float, 3> bbox_max = {500, 500, 0};
  const std::array<float, 3> inset_min = {10, 20, 30};
  const std::array<float, 3> inset_max = {40, 50, 60};
  Apply(scenic::NewSetViewPropertiesCmd(view_holder_id, bbox_min, bbox_max, inset_min, inset_max));

  // Test to make sure the bounding boxes are the same.
  BoundingBox test_bbox(vec3(10, 20, -70), vec3(460, 450, -60));
  const BoundingBox view_bounding_box = view_holder->GetLocalBoundingBox();
  EXPECT_EQ(test_bbox, view_bounding_box);

  // Test to make sure the view planes are the same.
  const std::vector<plane3>& view_planes = view_holder->clip_planes();
  std::vector<plane3> test_planes = test_bbox.CreatePlanes();
  EXPECT_EQ(view_planes.size(), test_planes.size());
  for (uint32_t p = 0; p < view_planes.size(); p++) {
    plane3 test_plane = test_planes[p];
    plane3 view_plane = view_planes[p];
    EXPECT_EQ(test_plane.dir(), view_plane.dir());
    EXPECT_EQ(test_plane.dist(), view_plane.dist());
  }
}

// Run a single test case on a view that's added to a ViewHolder after its
// properties are set to make sure that it still clips.
VK_TEST_F(ViewClippingTest, ClipSettingBeforeViewCreationTest) {
  uint32_t scene_id = 5;
  uint32_t view_id = 15;
  uint32_t view_holder_id = 30;

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  Apply(scenic::NewCreateSceneCmd(scene_id));
  Apply(
      scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token), "MyViewHolder"));

  ViewHolder* view_holder = FindResource<ViewHolder>(view_holder_id).get();
  EXPECT_TRUE(view_holder);

  const std::array<float, 3> bbox_min = {-5, -10, -15};
  const std::array<float, 3> bbox_max = {5, 10, 15};
  const std::array<float, 3> inset = {0, 0, 0};

  BoundingBox bbox(vec3(bbox_min[0], bbox_min[1], bbox_min[2]),
                   vec3(bbox_max[0], bbox_max[1], bbox_max[2]));

  Apply(scenic::NewSetViewPropertiesCmd(view_holder_id, bbox_min, bbox_max, inset, inset));
  const std::vector<plane3>& clip_planes = view_holder->clip_planes();

  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "MyView"));

  std::vector<plane3> test_planes = bbox.CreatePlanes();

  EXPECT_EQ(clip_planes.size(), test_planes.size());

  for (uint32_t p = 0; p < clip_planes.size(); p++) {
    plane3 test_plane = test_planes[p];
    plane3 view_plane = clip_planes[p];
    EXPECT_EQ(test_plane.dir(), view_plane.dir());
    EXPECT_EQ(test_plane.dist(), view_plane.dist());
  }
}

// This test is used to check that meshes get clipped properly
// by their view holder's clip planes when the EngineRendererVisitor
// traverses the scene.
VK_TEST_F(ViewClippingTest, SceneTraversal) {
  auto escher = escher::test::GetEscher()->GetWeakPtr();

  uint32_t scene_id = 5;
  uint32_t view_id = 15;
  uint32_t view_holder_id = 30;
  uint32_t shape_node_id = 50;
  uint32_t material_id = 60;
  uint32_t rect_id = 70;

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  const std::array<float, 3> bbox_min = {0, 0, kFar};
  const std::array<float, 3> bbox_max = {kWidth, kHeight, kNear};
  const std::array<float, 3> inset_min = {0, 0, 0};
  const std::array<float, 3> inset_max = {0, 0, 0};

  Apply(scenic::NewCreateSceneCmd(scene_id));

  Apply(
      scenic::NewCreateViewHolderCmd(view_holder_id, std::move(view_holder_token), "MyViewHolder"));

  Apply(scenic::NewCreateViewCmd(view_id, std::move(view_token), "MyView"));

  Apply(scenic::NewSetViewPropertiesCmd(view_holder_id, bbox_min, bbox_max, inset_min, inset_max));

  Apply(scenic::NewCreateShapeNodeCmd(shape_node_id));

  // Set shape to shape node.
  EXPECT_TRUE(Apply(scenic::NewCreateRoundedRectangleCmd(rect_id, 30.f, 40.f, 2.f, 4.f, 6.f, 8.f)));

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
  paper_scene->bounding_box = BoundingBox(vec3(0.f, 0.f, kFar), vec3(kWidth, kHeight, kNear));

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

  auto gpu_uploader = std::make_shared<BatchGpuUploader>(escher, frame->frame_number());
  auto layout_updater = std::make_unique<ImageLayoutUpdater>(escher);

  paper_renderer->BeginFrame(frame, gpu_uploader, paper_scene, {camera}, output_image);

  EngineRendererVisitor visitor(paper_renderer.get(), gpu_uploader.get(), layout_updater.get(),
                                /*hide_protected_memory=*/false, nullptr);
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
  cache.BeginFrame(gpu_uploader.get(), 0);

  const PaperShapeCacheEntry& entry2 = cache.GetRoundedRectMesh(spec, planes.data(), 6);

  // Cache entries should be identitcal.
  EXPECT_EQ(entry.mesh->num_vertices(), entry2.mesh->num_vertices());
  EXPECT_EQ(entry.num_indices, entry2.num_indices);
  EXPECT_EQ(entry.num_shadow_volume_indices, entry2.num_shadow_volume_indices);

  // End frame
  paper_renderer->FinalizeFrame();
  auto upload_semaphore = escher::Semaphore::New(escher->vk_device());
  auto layout_update_semaphore = escher::Semaphore::New(escher->vk_device());
  gpu_uploader->AddSignalSemaphore(upload_semaphore);
  gpu_uploader->Submit();
  layout_updater->AddSignalSemaphore(layout_update_semaphore);
  layout_updater->Submit();
  paper_renderer->EndFrame({std::move(upload_semaphore), std::move(layout_update_semaphore)});
  cache.EndFrame();

  auto frame_done_semaphore = Semaphore::New(escher->vk_device());
  frame->EndFrame(frame_done_semaphore, nullptr);

  output_image = nullptr;

  escher->vk_device().waitIdle();
  escher->Cleanup();

  // TODO(36855): Now Vulkan validation layer has errors:
  //   [ UNASSIGNED-CoreValidation-DrawState-InvalidImageLayout ] Object: 0x4e03b6e20810
  //   (Type = 6) | Submitted command buffer expects VkImage 0x49[]  (subresource:
  //   aspectMask 0x1 array layer 0, mip level 0) to be in layout
  //   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL--instead, current layout is
  //   VK_IMAGE_LAYOUT_UNDEFINED..
  SUPPRESS_VK_VALIDATION_ERRORS();
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
