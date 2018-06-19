// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_SHADER_PROGRAM_H_
#define LIB_ESCHER_VK_SHADER_PROGRAM_H_

#include "lib/escher/resources/resource.h"
#include "lib/escher/util/enum_cast.h"
#include "lib/escher/util/enum_count.h"
#include "lib/escher/util/hash.h"
#include "lib/escher/util/hash_map.h"
#include "lib/escher/vk/shader_module.h"

namespace escher {

class ResourceRecycler;

class ShaderProgram;
using ShaderProgramPtr = fxl::RefPtr<ShaderProgram>;

// ShaderProgram encapsulates a set of ShaderModules, which are used to generate
// vk::Pipelines.  This is done in collaboration with CommandBuffer; different
// pipelines may be required depending on the current CommandBuffer state.
// ShaderProgram listens for changes in the ShaderModules; whenever new SPIR-V
// is available, all existing pipelines are invalidated and will be lazily
// regenerated upon demand.
class ShaderProgram : public Resource, private ShaderModuleListener {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  // Graphics program.
  static ShaderProgramPtr NewGraphics(
      ResourceRecycler* resource_recycler,
      std::vector<ShaderModulePtr> shader_modules);

  // Compute program.
  static ShaderProgramPtr NewCompute(ResourceRecycler* resource_recycler,
                                     ShaderModulePtr shader_module);

  ~ShaderProgram() override;

  // NOTE: The following public methods are called by the CommandBuffer
  // implementation, and are not useful to Escher clients.

  // Return the pipeline layout common to all pipeline variants of this program.
  PipelineLayout* pipeline_layout();

  // Return the module corresponding to the specified shader stage, or nullptr
  // if the program has no shader for that stage (e.g. many graphics programs
  // will not have a geometry shader).
  const ShaderModulePtr& GetModuleForStage(ShaderStage stage) const;

  // Simple HashMap lookup and insertion.
  vk::Pipeline FindPipeline(Hash hash) const;
  void StashPipeline(Hash hash, vk::Pipeline pipeline);

 private:
  // Called by NewGraphics() and NewCompute(), respectively.
  ShaderProgram(ResourceRecycler* resource_recycler,
                std::vector<ShaderModulePtr> shader_modules);
  ShaderProgram(ResourceRecycler* resource_recycler,
                ShaderModulePtr shader_module);

  void ObtainPipelineLayout();

  // Used by OnShaderModuleUpdated() as an easy way to have the ResourceRecycler
  // keep the obsolete pipelines alive until safe to destroy them.
  explicit ShaderProgram(ResourceManager* owner);
  void OnShaderModuleUpdated(ShaderModule* shader_module) override;

  std::array<ShaderModulePtr, EnumCount<ShaderStage>()> shader_modules_;
  HashMap<Hash, vk::Pipeline> graphics_pipelines_;
  vk::Pipeline compute_pipeline_;

  PipelineLayoutPtr pipeline_layout_;
};

// Inline function definitions.

inline PipelineLayout* ShaderProgram::pipeline_layout() {
  if (!pipeline_layout_) {
    ObtainPipelineLayout();
  }
  return pipeline_layout_.get();
}

inline const ShaderModulePtr& ShaderProgram::GetModuleForStage(
    ShaderStage stage) const {
  FXL_DCHECK(stage != ShaderStage::kEnumCount);
  return shader_modules_[EnumCast(stage)];
}

inline vk::Pipeline ShaderProgram::FindPipeline(Hash hash) const {
  auto it = graphics_pipelines_.find(hash);
  return it != graphics_pipelines_.end() ? it->second : vk::Pipeline();
}

inline void ShaderProgram::StashPipeline(Hash hash, vk::Pipeline pipeline) {
  FXL_DCHECK(!FindPipeline(hash));
  graphics_pipelines_[hash] = pipeline;
}

}  // namespace escher

#endif  // LIB_ESCHER_VK_SHADER_PROGRAM_H_
