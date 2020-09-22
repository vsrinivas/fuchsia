// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_SHADER_PROGRAM_H_
#define SRC_UI_LIB_ESCHER_VK_SHADER_PROGRAM_H_

#include "src/ui/lib/escher/resources/resource.h"
#include "src/ui/lib/escher/third_party/granite/vk/pipeline_layout.h"
#include "src/ui/lib/escher/util/enum_cast.h"
#include "src/ui/lib/escher/util/enum_count.h"
#include "src/ui/lib/escher/util/hash.h"
#include "src/ui/lib/escher/util/hash_map.h"
#include "src/ui/lib/escher/vk/sampler.h"
#include "src/ui/lib/escher/vk/shader_module.h"

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
  static ShaderProgramPtr NewGraphics(ResourceRecycler* resource_recycler,
                                      std::vector<ShaderModulePtr> shader_modules);

  // Compute program.
  static ShaderProgramPtr NewCompute(ResourceRecycler* resource_recycler,
                                     ShaderModulePtr shader_module);

  ~ShaderProgram() override;

  // NOTE: The following public methods are called by the CommandBuffer
  // implementation, and are not useful to Escher clients.

  // Return the pipeline layout for this program, operating with the optional
  // immutable sampler passed in (pass the null vk::Sampler handle to opt-out).
  //
  // TODO(fxbug.dev/7291): This code-flow assumes that ShaderPrograms source from, at
  // most, a single sampler. This is a blocking bug for implementing, e.g.,
  // ES-159.
  PipelineLayoutPtr ObtainPipelineLayout(impl::PipelineLayoutCache* pipeline_layout_cache,
                                         const SamplerPtr& immutable_sampler);

  // Return the module corresponding to the specified shader stage, or nullptr
  // if the program has no shader for that stage (e.g. many graphics programs
  // will not have a geometry shader).
  const ShaderModulePtr& GetModuleForStage(ShaderStage stage) const;

  // Simple HashMap lookup and insertion.
  vk::Pipeline FindPipeline(Hash hash) const;
  void StashPipeline(Hash hash, vk::Pipeline pipeline);

  size_t stashed_graphics_pipeline_count() const { return graphics_pipelines_.size(); }

 private:
  friend class VulkanTester;

  // Called by NewGraphics() and NewCompute(), respectively.
  ShaderProgram(ResourceRecycler* resource_recycler, std::vector<ShaderModulePtr> shader_modules);
  ShaderProgram(ResourceRecycler* resource_recycler, ShaderModulePtr shader_module);

  // Used by ClearPipelineStash() as an easy way to have the ResourceRecycler keep the obsolete
  // pipelines alive until safe to destroy them.
  explicit ShaderProgram(ResourceManager* owner);
  void OnShaderModuleUpdated(ShaderModule* shader_module) override;
  void ClearPipelineStash();

  std::array<ShaderModulePtr, EnumCount<ShaderStage>()> shader_modules_;

  // TODO(fxbug.dev/7290): These are effectively strong references to vk::Pipelines --
  // it is assumed that this object will be responsible for deleting them when
  // they go out of scope. During normal execution (e.g., without a shader
  // refresh) this cache is never cleared.
  HashMap<Hash, vk::Pipeline> graphics_pipelines_;

  std::optional<impl::PipelineLayoutSpec> pipeline_layout_spec_;
};

// Inline function definitions.
inline const ShaderModulePtr& ShaderProgram::GetModuleForStage(ShaderStage stage) const {
  FX_DCHECK(stage != ShaderStage::kEnumCount);
  return shader_modules_[EnumCast(stage)];
}

inline vk::Pipeline ShaderProgram::FindPipeline(Hash hash) const {
  auto it = graphics_pipelines_.find(hash);
  return it != graphics_pipelines_.end() ? it->second : vk::Pipeline();
}

inline void ShaderProgram::StashPipeline(Hash hash, vk::Pipeline pipeline) {
  FX_DCHECK(!FindPipeline(hash));
  graphics_pipelines_[hash] = pipeline;
}

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_SHADER_PROGRAM_H_
