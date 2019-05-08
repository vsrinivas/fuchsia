// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/shader_program.h"

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/third_party/granite/vk/pipeline_layout.h"
#include "src/ui/lib/escher/third_party/granite/vk/shader_utils.h"
#include "src/ui/lib/escher/util/enum_cast.h"
#include "src/ui/lib/escher/util/hasher.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/impl/pipeline_layout_cache.h"

namespace escher {

const ResourceTypeInfo ShaderProgram::kTypeInfo("ShaderProgram",
                                                ResourceType::kResource,
                                                ResourceType::kShaderProgram);

ShaderProgramPtr ShaderProgram::NewGraphics(
    ResourceRecycler* resource_recycler,
    std::vector<ShaderModulePtr> shader_modules) {
  auto prog = new ShaderProgram(resource_recycler, std::move(shader_modules));
  return fxl::AdoptRef(prog);
}

ShaderProgramPtr ShaderProgram::NewCompute(ResourceRecycler* resource_recycler,
                                           ShaderModulePtr shader_module) {
  auto prog = new ShaderProgram(resource_recycler, std::move(shader_module));
  return fxl::AdoptRef(prog);
}

ShaderProgram::ShaderProgram(ResourceRecycler* resource_recycler,
                             std::vector<ShaderModulePtr> shader_modules)
    : Resource(resource_recycler) {
  for (auto& mod : shader_modules) {
    FXL_DCHECK(mod && mod->shader_stage() != ShaderStage::kEnumCount);
    auto index = EnumCast(mod->shader_stage());
    FXL_DCHECK(!shader_modules_[index]) << "multiply-defined stage.";
    mod->AddShaderModuleListener(this);
    shader_modules_[index] = std::move(mod);
  }
}

ShaderProgram::ShaderProgram(ResourceRecycler* resource_recycler,
                             ShaderModulePtr shader_module)
    : Resource(resource_recycler) {
  FXL_DCHECK(shader_module &&
             shader_module->shader_stage() == ShaderStage::kCompute);
  shader_module->AddShaderModuleListener(this);
  shader_modules_[EnumCast(ShaderStage::kCompute)] = std::move(shader_module);
}

ShaderProgram::~ShaderProgram() {
  for (auto& mod : shader_modules_) {
    if (mod) {
      mod->RemoveShaderModuleListener(this);
    }
  }
  vk::Device device = vk_device();
  for (auto& pipeline : graphics_pipelines_) {
    device.destroyPipeline(pipeline.second);
  }
  if (compute_pipeline_) {
    device.destroyPipeline(compute_pipeline_);
  }
}

ShaderProgram::ShaderProgram(ResourceManager* manager) : Resource(manager) {}

void ShaderProgram::OnShaderModuleUpdated(ShaderModule* shader_module) {
  if (pipeline_layout_) {
    // We must keep the obsolete pipelines for just as long as if this object's
    // ref-count hit zero.  The easiest way to do this is to move them into
    // another ShaderProgram and immediately deref it.
    auto keep_alive = new ShaderProgram(owner());
    keep_alive->KeepAlive(sequence_number());
    keep_alive->pipeline_layout_ = std::move(pipeline_layout_);
    keep_alive->graphics_pipelines_ = std::move(graphics_pipelines_);
    keep_alive->compute_pipeline_ = compute_pipeline_;

    // Clear values so that they will be lazily recreated when requested.
    pipeline_layout_ = nullptr;
    graphics_pipelines_.clear();
    compute_pipeline_ = vk::Pipeline();

    // Allow the ref-count to immediately hit zero.
    fxl::AdoptRef(keep_alive);
  }
}

void ShaderProgram::ObtainPipelineLayout() {
  TRACE_DURATION("gfx", "escher::ShaderProgram::ObtainPipelineLayout");
  FXL_DCHECK(graphics_pipelines_.empty());
  FXL_DCHECK(!compute_pipeline_);
  FXL_DCHECK(!pipeline_layout_);

  impl::PipelineLayoutSpec spec;
  impl::GeneratePipelineLayoutSpec(shader_modules_, &spec);

  // Compute a hash to quickly decide whether all descriptor sets must be
  // invalidated.
  Hasher h;
  h.struc(spec.push_constant_ranges);
  spec.push_constant_layout_hash = h.value();

  pipeline_layout_ =
      escher()->pipeline_layout_cache()->ObtainPipelineLayout(spec);

  // Unlike graphics pipelines, which may have multiple variants depending on
  // e.g. stencil buffer configuration, there is only ever one variant of a
  // compute shader.  Therefore, it can be created immediately.
  if (auto& compute_module = GetModuleForStage(ShaderStage::kCompute)) {
    TRACE_DURATION("gfx",
                   "escher::ShaderProgram::ObtainPipelineLayout [create "
                   "compute pipeline]");
    // Need to revisit when the Vulkan spec allows compute shaders to be used
    // within render passes.
    FXL_DCHECK(!GetModuleForStage(ShaderStage::kVertex));
    FXL_DCHECK(!GetModuleForStage(ShaderStage::kGeometry));
    FXL_DCHECK(!GetModuleForStage(ShaderStage::kFragment));
    FXL_DCHECK(!GetModuleForStage(ShaderStage::kTessellationControl));
    FXL_DCHECK(!GetModuleForStage(ShaderStage::kTessellationEvaluation));

    vk::ComputePipelineCreateInfo info;
    info.layout = pipeline_layout_->vk();
    info.stage.stage = vk::ShaderStageFlagBits::eCompute;
    info.stage.module = compute_module->vk();
    info.stage.pName = "main";
    compute_pipeline_ = ESCHER_CHECKED_VK_RESULT(
        vk_device().createComputePipeline(nullptr, info));
  }
}

}  // namespace escher
