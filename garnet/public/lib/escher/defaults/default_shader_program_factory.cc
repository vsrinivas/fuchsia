// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/defaults/default_shader_program_factory.h"

#include "lib/escher/escher.h"
#include "lib/escher/util/enum_cast.h"

namespace escher {

DefaultShaderProgramFactory::DefaultShaderProgramFactory(
    EscherWeakPtr escher, HackFilesystemPtr filesystem)
    : escher_(std::move(escher)), filesystem_(std::move(filesystem)) {}

DefaultShaderProgramFactory::~DefaultShaderProgramFactory() = default;

ShaderProgramPtr DefaultShaderProgramFactory::GetProgram(
    const std::string shader_paths[EnumCount<ShaderStage>()],
    ShaderVariantArgs args) {
  Hasher hasher(args.hash().val);
  for (size_t i = 0; i < EnumCount<ShaderStage>(); ++i) {
    hasher.string(shader_paths[i]);
  }
  Hash hash = hasher.value();

  auto& record = programs_[hash];
  if (record.ptr) {
#ifndef NDEBUG
    // Verify there is no hash collision.
    FXL_DCHECK(record.args == args);
    for (size_t i = 0; i < EnumCount<ShaderStage>(); ++i) {
      FXL_DCHECK(record.paths[i] == shader_paths[i]);
    }
#endif
    return record.ptr;
  }

  // No existing program was found; need to create a new one, either a graphics
  // program or a compute program, depending on the module paths that are
  // provided.
  const bool has_compute_stage =
      !shader_paths[EnumCast(ShaderStage::kCompute)].empty();
  const bool has_vertex_stage =
      !shader_paths[EnumCast(ShaderStage::kVertex)].empty();

  // Obtain a ShaderModule variant for each non-empty path.
  std::vector<ShaderModulePtr> modules;
  modules.reserve(EnumCount<ShaderStage>());
  for (size_t i = 0; i < EnumCount<ShaderStage>(); ++i) {
    if (!shader_paths[i].empty()) {
      ShaderStage stage = static_cast<ShaderStage>(i);
      modules.push_back(ObtainShaderModuleTemplate(stage, shader_paths[i])
                            ->GetShaderModuleVariant(args));
    }
  }

  // Compute programs can only have a single stage.  Every graphics program must
  // have a vertex stage, but not necessarily the others.
  FXL_DCHECK((has_compute_stage && modules.size() == 1) || has_vertex_stage);

  // Create the shader program.
  ShaderProgramPtr program =
      has_compute_stage
          ? ShaderProgram::NewCompute(escher_->resource_recycler(), modules[0])
          : ShaderProgram::NewGraphics(escher_->resource_recycler(),
                                       std::move(modules));

  // Remember the program in case it is requested again.
  record.ptr = program;
#ifndef NDEBUG
  for (size_t i = 0; i < EnumCount<ShaderStage>(); ++i) {
    record.paths[i] = shader_paths[i];
  }
  record.args = std::move(args);
#endif

  return program;
}

ShaderModuleTemplatePtr DefaultShaderProgramFactory::ObtainShaderModuleTemplate(
    ShaderStage stage, const std::string& source_path) {
  auto it = templates_.find(source_path);
  if (it != templates_.end()) {
    return it->second;
  }

  auto t = fxl::MakeRefCounted<ShaderModuleTemplate>(
      escher_->vk_device(), escher_->shaderc_compiler(), stage, source_path,
      filesystem_);
  templates_[source_path] = t;
  return t;
}

void DefaultShaderProgramFactory::Clear() {
  programs_.clear();
  templates_.clear();
}

}  // namespace escher
