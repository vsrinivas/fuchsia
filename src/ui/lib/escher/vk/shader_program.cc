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
}

ShaderProgram::ShaderProgram(ResourceManager* manager) : Resource(manager) {}

void ShaderProgram::OnShaderModuleUpdated(ShaderModule* shader_module) {
  ClearPipelineLayout();
  ClearPipelineStash();
}

void ShaderProgram::ClearPipelineLayout() {
  if (!pipeline_layout_)
    return;

  // We must keep the obsolete pipeline layout alive for just as long as it
  // takes for this object's ref-count to hit zero.  The easiest way to do this
  // is to move them into another ShaderProgram and immediately deref it.
  auto keep_alive = new ShaderProgram(owner());
  keep_alive->KeepAlive(sequence_number());
  keep_alive->pipeline_layout_ = std::move(pipeline_layout_);
  pipeline_layout_ = nullptr;

  // Allow the ref-count to immediately hit zero.
  fxl::AdoptRef(keep_alive);
}

void ShaderProgram::ClearPipelineStash() {
  if (graphics_pipelines_.empty())
    return;

  // We must keep the obsolete pipelines alive for just as long as it
  // takes for this object's ref-count to hit zero.  The easiest way to do this
  // is to move them into another ShaderProgram and immediately deref it.
  auto keep_alive = new ShaderProgram(owner());
  keep_alive->KeepAlive(sequence_number());
  keep_alive->graphics_pipelines_ = std::move(graphics_pipelines_);
  graphics_pipelines_.clear();

  // Allow the ref-count to immediately hit zero.
  fxl::AdoptRef(keep_alive);
}

PipelineLayout* ShaderProgram::ObtainPipelineLayout(
    const SamplerPtr& immutable_sampler) {
  TRACE_DURATION("gfx", "escher::ShaderProgram::ObtainPipelineLayout");
  // If we already have a pipeline layout, and the immutable sampler matches,
  // just use that.
  if (pipeline_layout_ &&
      pipeline_layout_->spec().immutable_sampler() == immutable_sampler) {
    return pipeline_layout_.get();
  }

  // Clear out the old pipeline layout, if there is one.
  ClearPipelineLayout();

  FXL_DCHECK(!pipeline_layout_);

  impl::PipelineLayoutSpec spec =
      impl::GeneratePipelineLayoutSpec(shader_modules_, immutable_sampler);

  // TODO(ES-201): This code assumes we only need to cache a single pipeline
  // layout per shader program. Immutable samplers ruin that assumption.
  pipeline_layout_ =
      escher()->pipeline_layout_cache()->ObtainPipelineLayout(spec);

  return pipeline_layout_.get();
}

}  // namespace escher
