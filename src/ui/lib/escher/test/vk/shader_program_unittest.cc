// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/shader_program.h"

#include "src/ui/lib/escher/defaults/default_shader_program_factory.h"
#include "src/ui/lib/escher/flatland/flatland_static_config.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/mesh/tessellation.h"
// TODO(ES-183): remove PaperRenderer shader dependency.
#include "src/ui/lib/escher/paper/paper_render_funcs.h"
#include "src/ui/lib/escher/paper/paper_renderer_static_config.h"
#include "src/ui/lib/escher/paper/paper_shape_cache.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/shaders/util/spirv_file_util.h"
#include "src/ui/lib/escher/shape/mesh.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/vk/vulkan_tester.h"
#include "src/ui/lib/escher/third_party/granite/vk/render_pass.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/util/string_utils.h"
#include "src/ui/lib/escher/vk/command_buffer.h"
#include "src/ui/lib/escher/vk/impl/pipeline_layout_cache.h"
#include "src/ui/lib/escher/vk/pipeline_builder.h"
#include "src/ui/lib/escher/vk/shader_module_template.h"
#include "src/ui/lib/escher/vk/shader_variant_args.h"
#include "src/ui/lib/escher/vk/texture.h"

#if ESCHER_USE_RUNTIME_GLSL
// This include requires a nogncheck to workaround a known GN issue that doesn't
// take ifdefs into account when checking includes, meaning that even when
// ESCHER_USE_RUNTIME_GLSL is false, GN will check the validity of this include
// and find that it shouldn't be allowed, since there is no shaderc when that
// macro is false. "nogncheck" prevents this.
#include "third_party/shaderc/libshaderc/include/shaderc/shaderc.hpp"  // nogncheck
#endif

#include <chrono>

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

// This test simply records the length of time in microseconds that it takes for
// Escher to load up all of the shaders it uses for PaperRenderer and Flatland.
// This is useful for getting a quick idea for how long this process takes on
// different platforms as well as depending on whether or not we're recompiling
// shader source code or loading in precompiled binaries.
VK_TEST_F(ShaderProgramTest, TimingTest) {
  auto escher = test::GetEscher();

  // Clear out the shader program factory's cache so that the test is hermetic and
  // does not change depending on whether or not previous tests have loaded these
  // shaders into the cache already.
  escher->shader_program_factory()->Clear();

  auto start = std::chrono::high_resolution_clock::now();
  auto program1 = escher->GetProgram(kAmbientLightProgramData);
  auto program2 = escher->GetProgram(kNoLightingProgramData);
  auto program3 = escher->GetProgram(kPointLightProgramData);
  auto program4 = escher->GetProgram(kPointLightFalloffProgramData);
  auto program5 = escher->GetProgram(kShadowVolumeGeometryProgramData);
  auto program6 = escher->GetProgram(kShadowVolumeGeometryDebugProgramData);
  auto program7 = escher->GetProgram(kFlatlandStandardProgram);
  auto stop = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
  FX_LOGS(INFO) << "Time taken to load shaders: " << duration.count() << " microseconds.";
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
  load_and_check_program(kFlatlandStandardProgram);
}

// Test to check to the "SpirvExistsOnDisk" function, which determines
// if the spirv contents of a file on disk have changed relative to a
// different spirv vector.
//
// This test checks against real Escher shader files, which means that
// it will fail if someone modifies a shader source file for Escher but
// forgets to run the precompile script to generate the spirv. This will
// help in keeping the precompiled shaders up to date.
//
// This test is only meant to be run locally by the Escher development team,
// as such it is not included on CQ at all, which has ESCHER_USE_RUNTIME_GLSL
// set to false by default. This is because it is possible for other teams
// (e.g. Spinel) to update the SpirV compiler, which would cause the new
// shader spirv to differ from that on disk, causing this test to fail. Since
// we do not want other teams to be burned with having to run the shader
// precompile script, it is the job of the Escher team to run this test locally
// when making shader changes to make sure all precompiled shaders are checked
// in successfully.
#if ESCHER_TEST_FOR_GLSL_SPIRV_MISMATCH
VK_TEST_F(ShaderProgramTest, SpirvNotChangedTest) {
  auto escher = test::GetEscher();
  auto filesystem = escher->shader_program_factory()->filesystem();

  auto check_spirv_change = [&](const ShaderProgramData& program_data) {
    // Loop over all the shader stages for the provided program.
    for (const auto& iter : program_data.source_files) {
      // Skip if path is empty.
      if (iter.second.length() == 0) {
        continue;
      }

      ShaderStage stage = iter.first;
      EXPECT_TRUE(program_data.source_files.count(stage));
      auto compiler = std::make_unique<shaderc::Compiler>();
      std::string shader_name = iter.second;
      auto shader = fxl::MakeRefCounted<ShaderModuleTemplate>(vk::Device(), compiler.get(), stage,
                                                              shader_name, filesystem);

      // The shader source code should still compile properly.
      std::vector<uint32_t> spirv;
      EXPECT_TRUE(shader->CompileVariantToSpirv(program_data.args, &spirv));

      // The new spirv should not be any different than the spirv that is already on disk.
      EXPECT_FALSE(shader_util::SpirvExistsOnDisk(
          program_data.args, *filesystem->base_path() + "/shaders/", shader_name, spirv));
    };
  };

  check_spirv_change(kAmbientLightProgramData);
  check_spirv_change(kNoLightingProgramData);
  check_spirv_change(kPointLightProgramData);
  check_spirv_change(kPointLightFalloffProgramData);
  check_spirv_change(kShadowVolumeGeometryProgramData);
  check_spirv_change(kShadowVolumeGeometryDebugProgramData);
  check_spirv_change(kFlatlandStandardProgram);
}
#endif  // ESCHER_TEST_FOR_GLSL_SPIRV_MISMATCH

VK_TEST_F(ShaderProgramTest, CachedVariants) {
  auto escher = test::GetEscher();

  // TODO(ES-183): remove PaperRenderer shader dependency.
  ShaderVariantArgs variant1({{"USE_ATTRIBUTE_UV", "1"},
                              {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"},
                              {"NO_SHADOW_LIGHTING_PASS", "1"}});
  ShaderVariantArgs variant2({{"USE_ATTRIBUTE_UV", "0"},
                              {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"},
                              {"NO_SHADOW_LIGHTING_PASS", "1"}});
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

VK_TEST_F(ShaderProgramTest, NonExistentShaderDeathTest) {
  auto escher = test::GetEscher();

  ShaderVariantArgs variant1({{"USE_ATTRIBUTE_UV", "1"},
                              {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"},
                              {"NO_SHADOW_LIGHTING_PASS", "1"}});
  const char* kNonExistentVert = "shaders/NON_EXISTENT_SHADER.vert";
  const char* kNonExistentFrag = "shaders/NON_EXISTENT_SHADER.frag";

  EXPECT_DEATH(
      { auto program = escher->GetGraphicsProgram(kNonExistentVert, kNonExistentFrag, variant1); },
      "");
}

// Helper function for tests below.  Typically clients only populate a RenderPassInfo; RenderPasses
// are lazily generated/cached internally by CommandBufferPipelineState::FlushGraphicsPipeline().
// This creates/returns an actual Vulkan render pass.
impl::RenderPassPtr CreateRenderPassForTest() {
  auto escher = test::GetEscher();

  // Use the same output format as Scenic screenshots.
  constexpr vk::Format kScenicScreenshotFormat = vk::Format::eB8G8R8A8Unorm;
  const vk::Format kDepthStencilFormat =
      ESCHER_CHECKED_VK_RESULT(escher->device()->caps().GetMatchingDepthStencilFormat(
          {vk::Format::eD24UnormS8Uint, vk::Format::eD32SfloatS8Uint}));

  RenderPassInfo::AttachmentInfo color_info;
  color_info.format = kScenicScreenshotFormat;
  color_info.swapchain_layout = vk::ImageLayout::eColorAttachmentOptimal;
  color_info.sample_count = 1;

  RenderPassInfo info;
  RenderPassInfo::InitRenderPassInfo(&info, color_info,
                                     /*depth_format=*/kDepthStencilFormat,
                                     /*msaa_format=*/vk::Format::eUndefined, /*sample_count=*/1,
                                     /*use_transient_depth_and_msaa*/ false);

  return fxl::MakeRefCounted<impl::RenderPass>(escher->resource_recycler(), info);
}

// Helper function which sets up vertex attribute bindings that will be used for pipeline creation
// in tests.  It doesn't really matter what vertex format is used, so we just use the standard one
// used by PaperShapeCache.
void SetupVertexAttributeBindingsForTest(CommandBufferPipelineState* cbps) {
  MeshSpec mesh_spec = PaperShapeCache::kStandardMeshSpec();
  const uint32_t total_attribute_count = mesh_spec.total_attribute_count();
  BlockAllocator allocator(512);
  RenderFuncs::VertexAttributeBinding* attribute_bindings =
      RenderFuncs::NewVertexAttributeBindings(PaperRenderFuncs::kMeshAttributeBindingLocations,
                                              &allocator, mesh_spec, total_attribute_count);

  for (uint32_t i = 0; i < total_attribute_count; ++i) {
    attribute_bindings[i].Bind(cbps);
  }
}

// This tests the most direct form of pipeline generation, without all of the laziness and caching
// done by CommandBuffer.  Fundamentally this requires 4 things:
//   1) a set of vk::ShaderModules
//   2) a vk::PipelineLayout
//   3) a vk::RenderPass
//   4) a description of the static Vulkan state that the pipeline will be used with
//
//   ShaderProgram provides both 1) and 2), the latter via introspecting each modules' SPIR-V code.
//   The test constructs 3) and 4).  The latter is achieved
// is provided directly by the ShaderProgram, and 2) is
//
// - a ShaderProgram, in particular:
//   - the set of vk::ShaderModules that it encapsulates
//   - the PipelineLayout* obtained by inspection of the shader modules' SPIR-V code
// - a MeshSpec, which defines the pipeline's vertex attribute bindings
// - a CommandBufferPipelineState, responsible for:
//   - representing all static state required to build a pipeline, i.e. everything except
//   - building the pipeline based on that state.
VK_TEST_F(ShaderProgramTest, GeneratePipelineDirectly) {
  auto escher = test::GetEscher();

  // 1), 2): obtain the ShaderProgram and the corresponding PipelineLayout.
  // TODO(ES-183): remove PaperRenderer shader dependency.
  auto program = ClearPipelineStash(escher->GetProgram(escher::kNoLightingProgramData));
  EXPECT_TRUE(program);
  PipelineLayoutPtr pipeline_layout =
      program->ObtainPipelineLayout(escher->pipeline_layout_cache(), nullptr);

  // 3): create a RenderPass.
  // NOTE: typically, RenderPasses are lazily generated/cached by
  // CommandBufferPipelineState::FlushGraphicsPipeline().
  impl::RenderPassPtr render_pass = CreateRenderPassForTest();

  // 4) Specify the static Vulkan state.
  CommandBufferPipelineState cbps(escher->pipeline_builder()->GetWeakPtr());
  SetupVertexAttributeBindingsForTest(&cbps);
  cbps.set_render_pass(render_pass.get());
  cbps.SetToDefaultState(CommandBuffer::DefaultState::kOpaque);

  // 5) Build a pipeline (smoke-test).
  EXPECT_EQ(0U, program->stashed_graphics_pipeline_count());
  vk::Pipeline pipeline_orig = cbps.FlushGraphicsPipeline(pipeline_layout.get(), program.get());
  EXPECT_EQ(1U, program->stashed_graphics_pipeline_count());

  // 6) Verify that, when blending is disabled, we get the same cached pipeline with different
  // blend-ops and blend-factors.
  const vk::BlendOp alpha_op = vk::BlendOp::eMin;
  const vk::BlendOp alpha_op_orig = cbps.static_state()->get_alpha_blend_op();
  EXPECT_NE(alpha_op, alpha_op_orig);  // otherwise the test is bogus
  const vk::BlendOp color_op = vk::BlendOp::eMin;
  const vk::BlendOp color_op_orig = cbps.static_state()->get_color_blend_op();
  EXPECT_NE(color_op, color_op_orig);  // otherwise the test is bogus
  const vk::BlendFactor dst_alpha_blend = vk::BlendFactor::eOne;
  const vk::BlendFactor src_alpha_blend = vk::BlendFactor::eOne;
  const vk::BlendFactor dst_color_blend = vk::BlendFactor::eOne;
  const vk::BlendFactor src_color_blend = vk::BlendFactor::eOne;
  const vk::BlendFactor dst_alpha_blend_orig = cbps.static_state()->get_dst_alpha_blend();
  const vk::BlendFactor src_alpha_blend_orig = cbps.static_state()->get_src_alpha_blend();
  const vk::BlendFactor dst_color_blend_orig = cbps.static_state()->get_dst_color_blend();
  const vk::BlendFactor src_color_blend_orig = cbps.static_state()->get_src_color_blend();
  EXPECT_NE(dst_alpha_blend, dst_alpha_blend_orig);  // otherwise the test is bogus
  EXPECT_NE(src_alpha_blend, src_alpha_blend_orig);  // otherwise the test is bogus
  EXPECT_NE(dst_color_blend, dst_color_blend_orig);  // otherwise the test is bogus
  EXPECT_NE(src_color_blend, src_color_blend_orig);  // otherwise the test is bogus

  cbps.SetBlendFactors(src_color_blend, src_alpha_blend, dst_color_blend, dst_alpha_blend);
  cbps.SetBlendOp(color_op, alpha_op);
  vk::Pipeline pipeline2 = cbps.FlushGraphicsPipeline(pipeline_layout.get(), program.get());
  EXPECT_EQ(pipeline_orig, pipeline2);

  // 7) Verify that, when blending is enabled, different blend-ops and blend-factors result in
  // different pipelines.
  cbps.SetBlendEnable(true);
  vk::Pipeline pipeline3 = cbps.FlushGraphicsPipeline(pipeline_layout.get(), program.get());
  cbps.SetBlendFactors(src_color_blend_orig, src_alpha_blend_orig, dst_color_blend_orig,
                       dst_alpha_blend_orig);
  vk::Pipeline pipeline4 = cbps.FlushGraphicsPipeline(pipeline_layout.get(), program.get());
  cbps.SetBlendOp(color_op_orig, alpha_op_orig);
  vk::Pipeline pipeline5 = cbps.FlushGraphicsPipeline(pipeline_layout.get(), program.get());
  EXPECT_NE(pipeline_orig, pipeline3);
  EXPECT_NE(pipeline_orig, pipeline4);
  EXPECT_NE(pipeline_orig, pipeline5);
  EXPECT_NE(pipeline3, pipeline4);
  EXPECT_NE(pipeline3, pipeline5);
  EXPECT_NE(pipeline4, pipeline5);

  // 8) Verify that, when blending is enabled, changing blend constants only makes a difference when
  // the blend-factor is eConstantColor.
  cbps.potential_static_state()->blend_constants[0] = 0.77f;
  cbps.potential_static_state()->blend_constants[3] = 0.66f;
  vk::Pipeline pipeline6 = cbps.FlushGraphicsPipeline(pipeline_layout.get(), program.get());
  EXPECT_EQ(pipeline5, pipeline6);
  cbps.potential_static_state()->blend_constants[0] = 0.55f;
  cbps.potential_static_state()->blend_constants[3] = 0.44f;
  vk::Pipeline pipeline7 = cbps.FlushGraphicsPipeline(pipeline_layout.get(), program.get());
  EXPECT_EQ(pipeline5, pipeline7);
  cbps.SetBlendFactors(vk::BlendFactor::eConstantColor, vk::BlendFactor::eConstantColor,
                       vk::BlendFactor::eConstantColor, vk::BlendFactor::eConstantColor);
  vk::Pipeline pipeline8 = cbps.FlushGraphicsPipeline(pipeline_layout.get(), program.get());
  EXPECT_NE(pipeline7, pipeline8);
  cbps.potential_static_state()->blend_constants[0] = 0.77f;
  cbps.potential_static_state()->blend_constants[3] = 0.66f;
  vk::Pipeline pipeline9 = cbps.FlushGraphicsPipeline(pipeline_layout.get(), program.get());
  EXPECT_NE(pipeline6, pipeline9);
  // This is similar to comparing 5 vs. 6, except this time the blend-factor is eConstantCOlor.
  EXPECT_NE(pipeline8, pipeline9);
}

VK_TEST_F(ShaderProgramTest, PipelineBuilder) {
  auto escher = test::GetEscher();

  // 1), 2): obtain the ShaderPrograms and the corresponding PipelineLayouts.
  // TODO(ES-183): remove PaperRenderer shader dependency.
  auto program1 = ClearPipelineStash(escher->GetProgram(escher::kNoLightingProgramData));
  auto program2 = ClearPipelineStash(escher->GetProgram(escher::kPointLightProgramData));
  EXPECT_TRUE(program1);
  EXPECT_TRUE(program2);
  PipelineLayoutPtr pipeline_layout1 =
      program1->ObtainPipelineLayout(escher->pipeline_layout_cache(), nullptr);
  PipelineLayoutPtr pipeline_layout2 =
      program2->ObtainPipelineLayout(escher->pipeline_layout_cache(), nullptr);

  // 3): create a RenderPass.
  // NOTE: typically, RenderPasses are lazily generated/cached by
  // CommandBufferPipelineState::FlushGraphicsPipeline().
  impl::RenderPassPtr render_pass = CreateRenderPassForTest();

  // 4) Specify the static Vulkan state.
  CommandBufferPipelineState cbps(escher->pipeline_builder()->GetWeakPtr());
  SetupVertexAttributeBindingsForTest(&cbps);
  cbps.set_render_pass(render_pass.get());
  cbps.SetToDefaultState(CommandBuffer::DefaultState::kOpaque);

  // 5) Set up two similar vk::GraphicsPipelineCreateInfo structs, one with stencil buffer enabled
  // and the other without.  These will be passed to PipelineBuilder instances.
  BlockAllocator allocator(128);
  auto create_info1 =
      cbps.InitGraphicsPipelineCreateInfo(&allocator, pipeline_layout1.get(), program1.get());
  cbps.SetStencilTest(true);
  auto create_info2 =
      cbps.InitGraphicsPipelineCreateInfo(&allocator, pipeline_layout2.get(), program2.get());

  // 6) This callback will be invoked after set_log_pipeline_creation_callback() has injected
  // it into a PipelineBuilder.
  size_t log_graphics_callback_count = 0;
  size_t log_compute_callback_count = 0;
  auto log_callback = [&log_graphics_callback_count, &log_compute_callback_count](
                          const vk::GraphicsPipelineCreateInfo* graphics_info,
                          const vk::ComputePipelineCreateInfo* compute_info) {
    EXPECT_TRUE(graphics_info || compute_info);
    EXPECT_TRUE(!graphics_info || !compute_info);
    if (graphics_info) {
      ++log_graphics_callback_count;
    } else {
      ++log_compute_callback_count;
    }
  };

  // Now we start testing the pipeline builder!

  // Test that we can create pipelines using a pipeline builder which doesn't us a VkPipeline cache.
  {
    PipelineBuilder builder(escher->vk_device());

    auto pipeline1 = builder.BuildGraphicsPipeline(*create_info1, /*do_logging=*/false);

    auto pipeline2 = builder.BuildGraphicsPipeline(*create_info2, /*do_logging=*/true);

    // Neither of the above pipelines resulted in logging, because no callback had been set.
    // After this, newly-built pipelines will trigger invocation of this callback, but only if the
    // |do_logging| arg is true.
    builder.set_log_pipeline_creation_callback(log_callback);

    auto pipeline3 = builder.BuildGraphicsPipeline(*create_info1, /*do_logging=*/false);
    EXPECT_EQ(0U, log_graphics_callback_count);
    auto pipeline4 = builder.BuildGraphicsPipeline(*create_info2, /*do_logging=*/true);
    EXPECT_EQ(1U, log_graphics_callback_count);
    auto pipeline5 = builder.BuildGraphicsPipeline(*create_info1, /*do_logging=*/true);
    EXPECT_EQ(2U, log_graphics_callback_count);
    auto pipeline6 = builder.BuildGraphicsPipeline(*create_info2, /*do_logging=*/true);
    EXPECT_EQ(3U, log_graphics_callback_count);

    ASSERT_TRUE(pipeline1);
    ASSERT_TRUE(pipeline2);
    ASSERT_TRUE(pipeline3);
    ASSERT_TRUE(pipeline4);
    ASSERT_TRUE(pipeline5);
    ASSERT_TRUE(pipeline6);
    escher->vk_device().destroyPipeline(pipeline1);
    escher->vk_device().destroyPipeline(pipeline2);
    escher->vk_device().destroyPipeline(pipeline3);
    escher->vk_device().destroyPipeline(pipeline4);
    escher->vk_device().destroyPipeline(pipeline5);
    escher->vk_device().destroyPipeline(pipeline6);
  }

  // Test that we can create pipelines using a VkPipelineCache, and that creating the "same"
  // pipeline twice does not result in a second invocation of the StorePipelineCacheDataCallback.

  // TODO(fxbug.dev/49692): SwiftShader ICD doesn't store cached pipeline to disk correctly.
  // So we disabled all the EXPECT checks on SwiftShader. We need to remove this
  // after the bug is solved.
  {
    // Keeps track of the number of times that a newly-built pipeline results in updated
    // cache data, which the application should persist to disk.
    size_t updated_vk_cache_data_count = 0;

    // Store the latest cache data obtained via the
    std::vector<uint8_t> latest_vk_cache_data;

    auto updated_vk_cache_data_callback = [&updated_vk_cache_data_count,
                                           &latest_vk_cache_data](std::vector<uint8_t> data) {
      ++updated_vk_cache_data_count;
      latest_vk_cache_data = std::move(data);
    };

    PipelineBuilder builder(escher->vk_device(), nullptr, 0, updated_vk_cache_data_callback);
    // This time we enable the logging callback from the beginning.
    builder.set_log_pipeline_creation_callback(log_callback);
    log_graphics_callback_count = log_compute_callback_count = 0;

    // The callback is not invoked eagerly when the pipeline is built, rather it is invoked when
    // MaybeStorePipelineCacheData() is polled.
    auto pipeline1a = builder.BuildGraphicsPipeline(*create_info1, /*do_logging=*/true);
    EXPECT_EQ(1U, log_graphics_callback_count);
    EXEC_IF_NOT_SWIFTSHADER(EXPECT_EQ(0U, updated_vk_cache_data_count));
    builder.MaybeStorePipelineCacheData();
    EXEC_IF_NOT_SWIFTSHADER(EXPECT_EQ(1U, updated_vk_cache_data_count));

    // Same thing, with different pipeline create info.
    auto pipeline2a = builder.BuildGraphicsPipeline(*create_info2, /*do_logging=*/true);
    EXPECT_EQ(2U, log_graphics_callback_count);
    EXEC_IF_NOT_SWIFTSHADER(EXPECT_EQ(1U, updated_vk_cache_data_count));
    builder.MaybeStorePipelineCacheData();
    EXEC_IF_NOT_SWIFTSHADER(EXPECT_EQ(2U, updated_vk_cache_data_count));

    // Creating additional pipelines with previously-seen create_info does not result in a change to
    // the persisted Vk cache data.
    auto pipeline1b = builder.BuildGraphicsPipeline(*create_info1, /*do_logging=*/true);
    auto pipeline2b = builder.BuildGraphicsPipeline(*create_info2, /*do_logging=*/true);
    builder.MaybeStorePipelineCacheData();
    EXPECT_EQ(4U, log_graphics_callback_count);
    EXEC_IF_NOT_SWIFTSHADER(EXPECT_EQ(2U, updated_vk_cache_data_count));

    // Create a new builder, primed with the data needed to build pipeline1 and pipeline2.  Building
    // these will not result in any new data to persist.
    PipelineBuilder builder2(escher->vk_device(), latest_vk_cache_data.data(),
                             latest_vk_cache_data.size(), updated_vk_cache_data_callback);
    // Build pipeline1/pipeline2 in the opposite order, just in case it makes a difference to the
    // particular Vulkan implementation.
    auto pipeline2c = builder.BuildGraphicsPipeline(*create_info2, /*do_logging=*/true);
    auto pipeline1c = builder.BuildGraphicsPipeline(*create_info1, /*do_logging=*/true);
    builder.MaybeStorePipelineCacheData();
    EXPECT_EQ(6U, log_graphics_callback_count);
    EXEC_IF_NOT_SWIFTSHADER(EXPECT_EQ(2U, updated_vk_cache_data_count));

    ASSERT_TRUE(pipeline1a);
    ASSERT_TRUE(pipeline1b);
    ASSERT_TRUE(pipeline1c);
    ASSERT_TRUE(pipeline2a);
    ASSERT_TRUE(pipeline2b);
    ASSERT_TRUE(pipeline2c);
    escher->vk_device().destroyPipeline(pipeline1a);
    escher->vk_device().destroyPipeline(pipeline1b);
    escher->vk_device().destroyPipeline(pipeline1c);
    escher->vk_device().destroyPipeline(pipeline2a);
    escher->vk_device().destroyPipeline(pipeline2b);
    escher->vk_device().destroyPipeline(pipeline2c);
  }
}

// TODO(ES-83): we need to set up so many meshes, materials, framebuffers, etc.
// before we can obtain pipelines, we might as well just make this an end-to-end
// test and actually render.  Or, go the other direction and manually set up
// state in a standalone CommandBufferPipelineState object.
// NOTE: see some of the other tests above, such as GeneratePipelineWithoutCommandBuffer...
// it is now possible to generate pipelines more directly.
//
// TODO(59139): Fix the test on Linux host.
VK_TEST_F(ShaderProgramTest,
#ifndef __linux__
          GeneratePipelines
#else
          DISABLED_GeneratePipelines
#endif
) {
  auto escher = test::GetEscher();

  // TODO(ES-183): remove PaperRenderer shader dependency.
  auto program = escher->GetProgram(escher::kNoLightingProgramData);
  EXPECT_TRUE(program);

  auto cb = CommandBuffer::NewForGraphics(escher, /*use_protected_memory=*/false);

  auto depth_format_result = escher->device()->caps().GetMatchingDepthFormat();
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

  // TODO(fxbug.dev/44566): simplify this test to not need images/command-buffers.
  RenderPassInfo::InitRenderPassAttachmentInfosFromImages(&render_pass_info);
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

// This tests if PipelineLayoutCache is keeping elements alive when ObtainPipelineLayout() is used.
VK_TEST_F(ShaderProgramTest, ObtainPipelineLayoutHitsPipelineLayoutCache) {
  auto escher = test::GetEscher();
  auto program = ClearPipelineStash(escher->GetProgram(escher::kNoLightingProgramData));
  EXPECT_TRUE(program);

  // We should use cache to generate pipeline layouts.
  auto* cache = escher->pipeline_layout_cache();
  cache->Clear();
  PipelineLayoutPtr pipeline_layout1 =
      program->ObtainPipelineLayout(escher->pipeline_layout_cache(), nullptr);
  EXPECT_EQ(1u, cache->size());
  PipelineLayoutPtr pipeline_layout2 =
      program->ObtainPipelineLayout(escher->pipeline_layout_cache(), nullptr);
  EXPECT_EQ(1u, cache->size());
  EXPECT_EQ(pipeline_layout1.get(), pipeline_layout2.get());

  // After a number of frames pipeline layout falls out of |cache|.
  const int kNumFrames = 5;
  uint64_t frame_number_ = 0;
  for (int i = 0; i < kNumFrames; ++i) {
    auto frame = escher->NewFrame("ShaderProgramTest", ++frame_number_);
    frame->EndFrame(SemaphorePtr(), [] {});
  }
  EXPECT_EQ(0u, cache->size());

  // ObtainPipelineLayout keeps pipeline layout alive.
  PipelineLayoutPtr first_pipeline_layout =
      program->ObtainPipelineLayout(escher->pipeline_layout_cache(), nullptr);
  for (int i = 0; i < kNumFrames; ++i) {
    auto frame = escher->NewFrame("ShaderProgramTest", ++frame_number_);
    PipelineLayoutPtr cur_pipeline_layout =
        program->ObtainPipelineLayout(escher->pipeline_layout_cache(), nullptr);
    frame->EndFrame(SemaphorePtr(), [] {});
    EXPECT_EQ(first_pipeline_layout.get(), cur_pipeline_layout.get());
  }
  EXPECT_EQ(1u, cache->size());
}

}  // anonymous namespace
