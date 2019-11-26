// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/shader_program.h"

#include "src/ui/lib/escher/defaults/default_shader_program_factory.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/mesh/tessellation.h"
#include "src/ui/lib/escher/paper/paper_renderer_static_config.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/shaders/util/spirv_file_util.h"
#include "src/ui/lib/escher/shape/mesh.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/test/vk/vulkan_tester.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/util/string_utils.h"
#include "src/ui/lib/escher/vk/command_buffer.h"
#include "src/ui/lib/escher/vk/shader_module_template.h"
#include "src/ui/lib/escher/vk/shader_variant_args.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace {
using namespace escher;

// TODO(SCN-1387): This number needs to be queried via sysmem or vulkan.
const uint32_t kYuvSize = 64;

class ShaderProgramTest : public ::testing::Test, public VulkanTester {
 protected:
  ShaderProgramTest()
      : vk_debug_report_callback_registry_(
            VK_TESTS_SUPPRESSED()
                ? nullptr
                : test::EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanInstance(),
            std::make_optional<VulkanInstance::DebugReportCallback>(
                test::impl::VkDebugReportCollector::HandleDebugReport, &vk_debug_report_collector_),
            {}),
        vk_debug_report_collector_() {}
  const MeshPtr& ring_mesh1() const { return ring_mesh1_; }
  const MeshPtr& ring_mesh2() const { return ring_mesh2_; }
  const MeshPtr& sphere_mesh() const { return sphere_mesh_; }

  test::impl::VkDebugReportCallbackRegistry& vk_debug_report_callback_registry() {
    return vk_debug_report_callback_registry_;
  }
  test::impl::VkDebugReportCollector& vk_debug_report_collector() {
    return vk_debug_report_collector_;
  }

 private:
  void SetUp() override {
    vk_debug_report_callback_registry().RegisterDebugReportCallbacks();
    auto escher = test::GetEscher();
    EXPECT_TRUE(escher->Cleanup());

    // TODO(ES-183): remove PaperRenderer shader dependency.
    auto factory = escher->shader_program_factory();
    bool success = factory->filesystem()->InitializeWithRealFiles({
        "shaders/model_renderer/default_position.vert",
        "shaders/model_renderer/main.frag",
        "shaders/model_renderer/main.vert",
        "shaders/model_renderer/shadow_map_generation.frag",
        "shaders/model_renderer/shadow_map_lighting.frag",
        "shaders/model_renderer/wobble_position.vert",
        "shaders/paper/common/use.glsl",
    });
    EXPECT_TRUE(success);

    BatchGpuUploader gpu_uploader(escher->GetWeakPtr());
    ring_mesh1_ = NewRingMesh(escher, &gpu_uploader,
                              MeshSpec{MeshAttribute::kPosition2D | MeshAttribute::kUV}, 8,
                              vec2(0.f, 0.f), 300.f, 200.f);
    ring_mesh2_ = NewRingMesh(escher, &gpu_uploader,
                              MeshSpec{MeshAttribute::kPosition2D | MeshAttribute::kUV}, 8,
                              vec2(0.f, 0.f), 400.f, 300.f);
    sphere_mesh_ = NewSphereMesh(escher, &gpu_uploader,
                                 MeshSpec{MeshAttribute::kPosition3D | MeshAttribute::kUV}, 8,
                                 vec3(0.f, 0.f, 0.f), 300.f);
    gpu_uploader.Submit();
    escher->vk_device().waitIdle();
  }

  void TearDown() override {
    ring_mesh1_ = ring_mesh2_ = sphere_mesh_ = nullptr;

    auto escher = test::GetEscher();
    escher->vk_device().waitIdle();
    EXPECT_TRUE(escher->Cleanup());

    escher->shader_program_factory()->Clear();

    EXPECT_VULKAN_VALIDATION_OK();
    vk_debug_report_callback_registry().DeregisterDebugReportCallbacks();
  }

  MeshPtr ring_mesh1_;
  MeshPtr ring_mesh2_;
  MeshPtr sphere_mesh_;

  test::impl::VkDebugReportCallbackRegistry vk_debug_report_callback_registry_;
  test::impl::VkDebugReportCollector vk_debug_report_collector_;
};

// Test to make sure that the shader data constants located in
// paper_renderer_static_config.h can be used to properly load
// vulkan shader programs.
VK_TEST_F(ShaderProgramTest, ShaderConstantsTest) {
  auto escher = test::GetEscher();

  auto program1 = escher->GetProgram(kAmbientLightProgramData);
  auto program2 = escher->GetProgram(kAmbientLightProgramData);
  auto program3 = escher->GetProgram(kShadowVolumeGeometryDebugProgramData);
  auto program4 = escher->GetProgram(kShadowVolumeGeometryDebugProgramData);

  // The first two programs use the same variant args, so should be identical,
  // and similarly with the last two.
  EXPECT_EQ(program1, program2);
  EXPECT_EQ(program3, program4);
  EXPECT_NE(program1, program3);
}

// Go through all of the shader programs in |paper_renderer_static_config.h| and make
// sure that all their spirv can be properly found on disk.
VK_TEST_F(ShaderProgramTest, SpirVReadFileTest) {
  auto escher = test::GetEscher();
  auto base_path = *escher->shader_program_factory()->filesystem()->base_path() + "/shaders/";
  auto load_and_check_program = [&](const ShaderProgramData& program) {
    for (const auto& iter : program.source_files) {
      std::vector<uint32_t> spirv;
      if (iter.second.size() == 0) {
        continue;
      }
      EXPECT_TRUE(shader_util::ReadSpirvFromDisk(program.args, base_path, iter.second, &spirv))
          << iter.second;
      EXPECT_TRUE(spirv.size() > 0);
    }
  };

  load_and_check_program(kAmbientLightProgramData);
  load_and_check_program(kNoLightingProgramData);
  load_and_check_program(kPointLightProgramData);
  load_and_check_program(kPointLightFalloffProgramData);
  load_and_check_program(kShadowVolumeGeometryProgramData);
  load_and_check_program(kShadowVolumeGeometryDebugProgramData);
}

VK_TEST_F(ShaderProgramTest, CachedVariants) {
  auto escher = test::GetEscher();

  // TODO(ES-183): remove PaperRenderer shader dependency.
  ShaderVariantArgs variant1({{"NO_SHADOW_LIGHTING_PASS", "1"},
                              {"USE_ATTRIBUTE_UV", "1"},
                              {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"}});
  ShaderVariantArgs variant2({{"NO_SHADOW_LIGHTING_PASS", "1"},
                              {"USE_ATTRIBUTE_UV", "0"},
                              {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"}});

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

  // TODO(ES-183): remove PaperRenderer shader dependency.
  auto program = escher->GetProgram(escher::kNoLightingProgramData);
  EXPECT_TRUE(program);

  auto cb = CommandBuffer::NewForGraphics(escher, /*use_protected_memory=*/false);

  auto depth_format_result = impl::GetSupportedDepthStencilFormat(escher->vk_physical_device());
  bool has_depth_attachment = depth_format_result.result != vk::Result::eSuccess;

  auto color_attachment =
      escher->NewAttachmentTexture(vk::Format::eB8G8R8A8Unorm, 512, 512, 1, vk::Filter::eNearest);
  auto depth_attachment = has_depth_attachment
                              ? escher->NewAttachmentTexture(depth_format_result.value, 512, 512, 1,
                                                             vk::Filter::eNearest)
                              : TexturePtr();

  // TODO(ES-83): add support for setting an initial image layout (is there
  // already a bug for this?  If not, add one).  Then, use this so we don't need
  // to immediately set a barrier on the new color attachment.
  // Alternately/additionally, note that we don't need to do this for the depth
  // attachment (because we aren't loading it we can treat it as initially
  // eUndefined)... there's no reason that we shouldn't be able to do this for
  // the color attachment too.
  cb->ImageBarrier(
      color_attachment->image(), vk::ImageLayout::eUndefined,
      vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits::eTopOfPipe,
      vk::AccessFlags(), vk::PipelineStageFlagBits::eColorAttachmentOutput,
      vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite);

  RenderPassInfo render_pass_info;
  render_pass_info.color_attachments[0] = color_attachment;
  render_pass_info.num_color_attachments = 1;
  // Clear and store color attachment 0, the sole color attachment.
  render_pass_info.clear_attachments = 1u;
  render_pass_info.store_attachments = 1u;
  render_pass_info.depth_stencil_attachment = depth_attachment;
  render_pass_info.op_flags = RenderPassInfo::kOptimalColorLayoutOp;
  if (depth_attachment) {
    render_pass_info.op_flags |=
        RenderPassInfo::kClearDepthStencilOp | RenderPassInfo::kOptimalDepthStencilLayoutOp;
  }
  EXPECT_TRUE(render_pass_info.Validate());

  // TODO(ES-83): move into ShaderProgramTest.
  BatchGpuUploader gpu_uploader(escher->GetWeakPtr(), 0);
  auto noise_image = image_utils::NewNoiseImage(escher->image_cache(), &gpu_uploader, 512, 512);
  auto upload_semaphore = escher::Semaphore::New(escher->vk_device());
  gpu_uploader.AddSignalSemaphore(upload_semaphore);
  gpu_uploader.Submit();
  cb->AddWaitSemaphore(std::move(upload_semaphore), vk::PipelineStageFlagBits::eFragmentShader);
  auto noise_texture = escher->NewTexture(noise_image, vk::Filter::eLinear);

  cb->BeginRenderPass(render_pass_info);

  // Setting the program doesn't immediately result in a pipeline being set.
  cb->SetShaderProgram(program);
  EXPECT_EQ(GetCurrentVkPipeline(cb), vk::Pipeline());

  // We'll use the same texture for both meshes.
  cb->BindTexture(1, 1, noise_texture);

  auto mesh = ring_mesh1();
  auto ab = &mesh->attribute_buffer(0);

  cb->BindIndices(mesh->index_buffer(), mesh->index_buffer_offset(), vk::IndexType::eUint32);

  cb->BindVertices(0, ab->buffer, ab->offset, ab->stride);
  cb->SetVertexAttributes(0, 0, vk::Format::eR32G32Sfloat,
                          mesh->spec().attribute_offset(0, MeshAttribute::kPosition2D));
  cb->SetVertexAttributes(0, 2, vk::Format::eR32G32Sfloat,
                          mesh->spec().attribute_offset(0, MeshAttribute::kUV));

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
  ab = &mesh->attribute_buffer(0);

  cb->BindIndices(mesh->index_buffer(), mesh->index_buffer_offset(), vk::IndexType::eUint32);

  cb->BindVertices(0, ab->buffer, ab->offset, ab->stride);

  cb->SetVertexAttributes(0, 0, vk::Format::eR32G32Sfloat,
                          mesh->spec().attribute_offset(0, MeshAttribute::kPosition2D));
  cb->SetVertexAttributes(0, 2, vk::Format::eR32G32Sfloat,
                          mesh->spec().attribute_offset(0, MeshAttribute::kUV));

  EXPECT_EQ(depth_readonly_pipeline, ObtainGraphicsPipeline(cb));

  // Changing to a mesh with a different layout results in a different pipeline.
  mesh = sphere_mesh();
  ab = &mesh->attribute_buffer(0);

  cb->BindIndices(mesh->index_buffer(), mesh->index_buffer_offset(), vk::IndexType::eUint32);

  cb->BindVertices(0, ab->buffer, ab->offset, ab->stride);

  cb->SetVertexAttributes(0, 0, vk::Format::eR32G32B32Sfloat,
                          mesh->spec().attribute_offset(0, MeshAttribute::kPosition3D));
  cb->SetVertexAttributes(2, 0, vk::Format::eR32G32Sfloat,
                          mesh->spec().attribute_offset(0, MeshAttribute::kUV));

  EXPECT_NE(depth_readonly_pipeline, ObtainGraphicsPipeline(cb));
  EXPECT_NE(vk::Pipeline(), ObtainGraphicsPipeline(cb));

  vk::Pipeline last_pipeline = ObtainGraphicsPipeline(cb);

  // Switching to an immutable sampler changes the pipeline.
  ImageInfo info;
  info.width = kYuvSize;
  info.height = kYuvSize;
  info.format = vk::Format::eG8B8R82Plane420Unorm;
  info.usage = vk::ImageUsageFlagBits::eSampled;
  info.is_mutable = false;

  auto yuv_image = escher->image_cache()->NewImage(info);
  auto yuv_texture = escher->NewTexture(yuv_image, vk::Filter::eLinear);

  EXPECT_TRUE(yuv_texture->sampler()->is_immutable());

  cb->SetShaderProgram(program, yuv_texture->sampler());
  EXPECT_NE(last_pipeline, ObtainGraphicsPipeline(cb));
  EXPECT_NE(vk::Pipeline(), ObtainGraphicsPipeline(cb));

  auto yuv_pipeline = ObtainGraphicsPipeline(cb);
  last_pipeline = ObtainGraphicsPipeline(cb);

  // Using the same sampler does not.
  cb->SetShaderProgram(program, yuv_texture->sampler());
  EXPECT_EQ(last_pipeline, ObtainGraphicsPipeline(cb));
  EXPECT_NE(vk::Pipeline(), ObtainGraphicsPipeline(cb));

  last_pipeline = ObtainGraphicsPipeline(cb);

  // Using a different sampler does cause the pipeline to change, because
  // immutable samplers require custom descriptor sets, and pipelines are bound
  // to specific descriptor sets at construction time.
  cb->SetShaderProgram(program, noise_texture->sampler());
  EXPECT_NE(last_pipeline, ObtainGraphicsPipeline(cb));
  EXPECT_NE(vk::Pipeline(), ObtainGraphicsPipeline(cb));

  last_pipeline = ObtainGraphicsPipeline(cb);

  // Using the previous YUV sampler reuses the old pipeline.
  cb->SetShaderProgram(program, yuv_texture->sampler());
  EXPECT_NE(last_pipeline, ObtainGraphicsPipeline(cb));
  EXPECT_NE(vk::Pipeline(), ObtainGraphicsPipeline(cb));
  EXPECT_EQ(yuv_pipeline, ObtainGraphicsPipeline(cb));

  cb->EndRenderPass();

  // TODO(ES-83): ideally only submitted CommandBuffers would need to be
  // cleaned up: if a never-submitted CB is destroyed, then it shouldn't
  // keep anything alive, and it shouldn't cause problems in e.g.
  // CommandBufferPool due to a forever-straggling buffer.
  EXPECT_TRUE(cb->Submit(nullptr));
}

}  // anonymous namespace
