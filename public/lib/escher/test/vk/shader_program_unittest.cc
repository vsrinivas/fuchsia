// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/shader_program.h"

#include "garnet/public/lib/escher/test/gtest_escher.h"
#include "garnet/public/lib/escher/test/vk/vulkan_tester.h"

#include "lib/escher/defaults/default_shader_program_factory.h"
#include "lib/escher/geometry/clip_planes.h"
#include "lib/escher/geometry/tessellation.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/util/string_utils.h"
#include "lib/escher/vk/command_buffer.h"
#include "lib/escher/vk/shader_module_template.h"
#include "lib/escher/vk/shader_variant_args.h"
#include "lib/escher/vk/texture.h"

namespace {
using namespace escher;

class ShaderProgramTest : public ::testing::Test, public VulkanTester {
 protected:
  const MeshPtr& ring_mesh1() const { return ring_mesh1_; }
  const MeshPtr& ring_mesh2() const { return ring_mesh2_; }
  const MeshPtr& sphere_mesh() const { return sphere_mesh_; }

 private:
  void SetUp() override {
    auto escher = test::GetEscher();
    EXPECT_TRUE(escher->Cleanup());

    auto factory = escher->shader_program_factory();
    bool success = factory->filesystem()->InitializeWithRealFiles(
        {"shaders/model_renderer/default_position.vert",
         "shaders/model_renderer/main.frag", "shaders/model_renderer/main.vert",
         "shaders/model_renderer/shadow_map_generation.frag",
         "shaders/model_renderer/shadow_map_lighting.frag",
         "shaders/model_renderer/wobble_position.vert"});
    EXPECT_TRUE(success);

    ring_mesh1_ = NewRingMesh(
        escher, MeshSpec{MeshAttribute::kPosition2D | MeshAttribute::kUV}, 8,
        vec2(0.f, 0.f), 300.f, 200.f);
    ring_mesh2_ = NewRingMesh(
        escher, MeshSpec{MeshAttribute::kPosition2D | MeshAttribute::kUV}, 8,
        vec2(0.f, 0.f), 400.f, 300.f);
    sphere_mesh_ = NewSphereMesh(
        escher, MeshSpec{MeshAttribute::kPosition3D | MeshAttribute::kUV}, 8,
        vec3(0.f, 0.f, 0.f), 300.f);
  }

  void TearDown() override {
    ring_mesh1_ = ring_mesh2_ = sphere_mesh_ = nullptr;

    auto escher = test::GetEscher();
    escher->vk_device().waitIdle();
    EXPECT_TRUE(escher->Cleanup());

    escher->shader_program_factory()->Clear();
  }

  MeshPtr ring_mesh1_;
  MeshPtr ring_mesh2_;
  MeshPtr sphere_mesh_;
};

VK_TEST_F(ShaderProgramTest, CachedVariants) {
  auto escher = test::GetEscher();

  ShaderVariantArgs variant1(
      {{"NO_SHADOW_LIGHTING_PASS", "1"},
       {"USE_UV_ATTRIBUTE", "1"},
       {"NUM_CLIP_PLANES", ToString(ClipPlanes::kNumPlanes)}});
  ShaderVariantArgs variant2(
      {{"NO_SHADOW_LIGHTING_PASS", "1"},
       {"USE_UV_ATTRIBUTE", "0"},
       {"NUM_CLIP_PLANES", ToString(ClipPlanes::kNumPlanes)}});

  const char* kMainVert = "shaders/model_renderer/main.vert";
  const char* kMainFrag = "shaders/model_renderer/main.frag";

  auto program1 = escher->GetGraphicsProgram(kMainVert, kMainFrag, variant1);
  auto program2 = escher->GetGraphicsProgram(kMainVert, kMainFrag, variant1);
  auto program3 = escher->GetGraphicsProgram(kMainVert, kMainFrag, variant2);
  auto program4 = escher->GetGraphicsProgram(kMainVert, kMainFrag, variant2);

  // The first two programs use the same variant args, so should be identical,
  // and similarly with the last two.
  EXPECT_EQ(program1, program2);
  EXPECT_EQ(program3, program4);
  EXPECT_NE(program1, program3);
}

// TODO(ES-83): we need to set up so many meshes, materials, framebuffers, etc.
// before we can obtain pipelines, we might as well just make this an end-to-end
// test and actually render.  Or, go the other direction and manually set up
// state in a standalone CommandBufferPipelineState object.
VK_TEST_F(ShaderProgramTest, GeneratePipelines) {
  auto escher = test::GetEscher();

  ShaderVariantArgs variant(
      {{"NO_SHADOW_LIGHTING_PASS", "1"},
       {"USE_UV_ATTRIBUTE", "1"},
       {"NUM_CLIP_PLANES", ToString(ClipPlanes::kNumPlanes)}});

  auto program =
      escher->GetGraphicsProgram("shaders/model_renderer/main.vert",
                                 "shaders/model_renderer/main.frag", variant);

  auto cb = CommandBuffer::NewForGraphics(escher);

  auto color_attachment = escher->NewAttachmentTexture(
      vk::Format::eB8G8R8A8Unorm, 512, 512, 1, vk::Filter::eNearest);
  auto depth_attachment = escher->NewAttachmentTexture(
      vk::Format::eD24UnormS8Uint, 512, 512, 1, vk::Filter::eNearest);

  // TODO(ES-83): add support for setting an initial image layout (is there
  // already a bug for this?  If not, add one).  Then, use this so we don't need
  // to immediately set a barrier on the new color attachment.
  // Alternately/additionally, note that we don't need to do this for the depth
  // attachment (because we aren't loading it we can treat it as initially
  // eUndefined)... there's no reason that we shouldn't be able to do this for
  // the color attachment too.
  cb->ImageBarrier(color_attachment->image(), vk::ImageLayout::eUndefined,
                   vk::ImageLayout::eColorAttachmentOptimal,
                   vk::PipelineStageFlagBits::eTopOfPipe, vk::AccessFlags(),
                   vk::PipelineStageFlagBits::eColorAttachmentOutput,
                   vk::AccessFlagBits::eColorAttachmentRead |
                       vk::AccessFlagBits::eColorAttachmentWrite);

  RenderPassInfo render_pass_info;
  render_pass_info.color_attachments[0] = color_attachment;
  render_pass_info.num_color_attachments = 1;
  // Clear and store color attachment 0, the sole color attachment.
  render_pass_info.clear_attachments = 1u;
  render_pass_info.store_attachments = 1u;
  render_pass_info.depth_stencil_attachment = depth_attachment;
  render_pass_info.op_flags = RenderPassInfo::kClearDepthStencilOp |
                              RenderPassInfo::kOptimalColorLayoutOp |
                              RenderPassInfo::kOptimalDepthStencilLayoutOp;
  EXPECT_TRUE(render_pass_info.Validate());

  // TODO(ES-83): move into ShaderProgramTest.
  auto noise_image = escher->NewNoiseImage(512, 512);
  auto noise_texture = escher->NewTexture(noise_image, vk::Filter::eLinear);
  cb->impl()->TakeWaitSemaphore(noise_image,
                                vk::PipelineStageFlagBits::eFragmentShader);

  cb->BeginRenderPass(render_pass_info);

  // Setting the program doesn't immediately result in a pipeline being set.
  cb->SetShaderProgram(program);
  EXPECT_EQ(GetCurrentVkPipeline(cb), vk::Pipeline());

  // We'll use the same texture for both meshes.
  cb->BindTexture(1, 1, noise_texture);

  auto mesh = ring_mesh1();

  cb->BindIndices(mesh->index_buffer(), mesh->index_buffer_offset(),
                  vk::IndexType::eUint32);

  cb->BindVertices(0, mesh->vertex_buffer(), mesh->vertex_buffer_offset(),
                   mesh->spec().GetStride());
  cb->SetVertexAttributes(
      0, 0, vk::Format::eR32G32Sfloat,
      mesh->spec().GetAttributeOffset(MeshAttribute::kPosition2D));
  cb->SetVertexAttributes(0, 2, vk::Format::eR32G32Sfloat,
                          mesh->spec().GetAttributeOffset(MeshAttribute::kUV));

  // Set the command buffer to a known default state, and obtain a pipeline.
  cb->SetToDefaultState(CommandBuffer::DefaultState::kOpaque);

  auto depth_read_write_pipeline = ObtainGraphicsPipeline(cb);
  EXPECT_NE(depth_read_write_pipeline, vk::Pipeline());

  // Requesting another pipeline with the same state returns the same cached
  // pipeline.
  EXPECT_EQ(depth_read_write_pipeline, ObtainGraphicsPipeline(cb));

  // Changing the state results in a different pipeline being returned.
  cb->SetDepthTestAndWrite(true, false);
  auto depth_readonly_pipeline = ObtainGraphicsPipeline(cb);
  EXPECT_NE(depth_readonly_pipeline, vk::Pipeline());
  EXPECT_NE(depth_readonly_pipeline, depth_read_write_pipeline);

  // Requesting another pipeline with the same state returns the same cached
  // pipeline.
  EXPECT_EQ(depth_readonly_pipeline, ObtainGraphicsPipeline(cb));

  // Changing to a different mesh with the same layout doesn't change the
  // obtained pipeline.
  mesh = ring_mesh2();

  cb->BindIndices(mesh->index_buffer(), mesh->index_buffer_offset(),
                  vk::IndexType::eUint32);

  cb->BindVertices(0, mesh->vertex_buffer(), mesh->vertex_buffer_offset(),
                   mesh->spec().GetStride());

  cb->SetVertexAttributes(
      0, 0, vk::Format::eR32G32Sfloat,
      mesh->spec().GetAttributeOffset(MeshAttribute::kPosition2D));
  cb->SetVertexAttributes(0, 2, vk::Format::eR32G32Sfloat,
                          mesh->spec().GetAttributeOffset(MeshAttribute::kUV));

  EXPECT_EQ(depth_readonly_pipeline, ObtainGraphicsPipeline(cb));

  // Changing to a mesh with a different layout results in a different pipeline.
  // pipeline.
  mesh = sphere_mesh();

  cb->BindIndices(mesh->index_buffer(), mesh->index_buffer_offset(),
                  vk::IndexType::eUint32);

  cb->BindVertices(0, mesh->vertex_buffer(), mesh->vertex_buffer_offset(),
                   mesh->spec().GetStride());

  cb->SetVertexAttributes(
      0, 0, vk::Format::eR32G32B32Sfloat,
      mesh->spec().GetAttributeOffset(MeshAttribute::kPosition3D));
  cb->SetVertexAttributes(2, 0, vk::Format::eR32G32Sfloat,
                          mesh->spec().GetAttributeOffset(MeshAttribute::kUV));

  EXPECT_NE(depth_readonly_pipeline, ObtainGraphicsPipeline(cb));
  EXPECT_NE(vk::Pipeline(), ObtainGraphicsPipeline(cb));

  cb->EndRenderPass();

  // TODO(ES-83): ideally only submitted CommandBuffers would need to be
  // cleaned up: if a never-submitted CB is destroyed, then it shouldn't
  // keep anything alive, and it shouldn't cause problems in e.g.
  // CommandBufferPool due to a forever-straggling buffer.
  EXPECT_TRUE(cb->Submit(nullptr));
}

}  // anonymous namespace
