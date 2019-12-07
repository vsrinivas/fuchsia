// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/defaults/default_shader_program_factory.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/escher_process_init.h"
#include "src/ui/lib/escher/flatland/flatland_static_config.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/fs/hack_filesystem.h"
#include "src/ui/lib/escher/hmd/pose_buffer_latching_shader.h"
#include "src/ui/lib/escher/impl/glsl_compiler.h"  // nogncheck
#include "src/ui/lib/escher/paper/paper_renderer_config.h"
#include "src/ui/lib/escher/paper/paper_renderer_static_config.h"
#include "src/ui/lib/escher/shaders/util/spirv_file_util.h"
#include "src/ui/lib/escher/vk/shader_program.h"

#include "third_party/shaderc/libshaderc/include/shaderc/shaderc.hpp"  // nogncheck

namespace escher {

// Compute shaders currently make use of the glslang compiler and not the shaderc
// compiler, and they do not take ShaderVariantArgs, so this function is tailored
// to compile compute shaders specifically.
bool CompileAndWriteComputeShader(HackFilesystemPtr filesystem, const char* source_code,
                                  const std::string& name) {
  std::string abs_root = *filesystem->base_path() + "/shaders/spirv/";

  static escher::impl::GlslToSpirvCompiler compiler;

  std::string code(source_code);

  impl::SpirvData spirv =
      compiler.Compile(vk::ShaderStageFlagBits::eCompute, {{code.c_str()}}, std::string(), "main")
          .get();

  if (shader_util::WriteSpirvToDisk(spirv, {}, abs_root, name)) {
    FXL_LOG(INFO) << "Processing compute shader " << name;
    return true;
  }
  FXL_LOG(ERROR) << "could not write shader " << name << " to disk.";

  return false;
}

// Compiles all of the provided shader modules and writes out their spirv
// to disk in the source tree.
bool CompileAndWriteShader(HackFilesystemPtr filesystem, ShaderProgramData program_data) {
  std::string abs_root = *filesystem->base_path() + "/shaders/spirv/";

  static auto compiler = std::make_unique<shaderc::Compiler>();

  // Loop over all the shader stages.
  for (const auto& iter : program_data.source_files) {
    // Skip if path is empty.
    if (iter.second.length() == 0) {
      continue;
    }

    FXL_LOG(INFO) << "Processing shader " << iter.second;

    auto shader = fxl::MakeRefCounted<ShaderModuleTemplate>(vk::Device(), compiler.get(),
                                                            iter.first, iter.second, filesystem);

    std::vector<uint32_t> spirv;
    if (!shader->CompileVariantToSpirv(program_data.args, &spirv)) {
      FXL_LOG(ERROR) << "could not compile shader " << iter.second;
      return false;
    }

    if (!shader_util::WriteSpirvToDisk(spirv, program_data.args, abs_root, iter.second)) {
      FXL_LOG(ERROR) << "could not write shader " << iter.second << " to disk.";
      return false;
    }
  }
  return true;
}

}  // namespace escher

// Program entry point.
int main(int argc, const char** argv) {
  // Register all the shader files, along with include files, that are used by Escher.
  auto filesystem = escher::HackFilesystem::New();

  // The binary for this is expected to be in ./out/default/host_x64.

  auto paths = escher::kPaperRendererShaderPaths;
  paths.insert(paths.end(), escher::kFlatlandShaderPaths.begin(),
               escher::kFlatlandShaderPaths.end());
  bool success = filesystem->InitializeWithRealFiles(paths, "./../../../../src/ui/lib/escher/");
  FXL_CHECK(success);
  FXL_CHECK(filesystem->base_path());

  // Ambient light program.
  if (!CompileAndWriteShader(filesystem, escher::kAmbientLightProgramData)) {
    return EXIT_FAILURE;
  }

  // No lighting program.
  if (!CompileAndWriteShader(filesystem, escher::kNoLightingProgramData)) {
    return EXIT_FAILURE;
  }

  if (!CompileAndWriteShader(filesystem, escher::kPointLightProgramData)) {
    return EXIT_FAILURE;
  }

  if (!CompileAndWriteShader(filesystem, escher::kPointLightFalloffProgramData)) {
    return EXIT_FAILURE;
  }

  if (!CompileAndWriteShader(filesystem, escher::kShadowVolumeGeometryProgramData)) {
    return EXIT_FAILURE;
  }

  if (!CompileAndWriteShader(filesystem, escher::kShadowVolumeGeometryDebugProgramData)) {
    return EXIT_FAILURE;
  }

  if (!CompileAndWriteComputeShader(filesystem, escher::hmd::g_kernel_src,
                                    escher::hmd::kPoseLatchingShaderName)) {
    return EXIT_FAILURE;
  }

  // Flatland shader.
  if (!CompileAndWriteShader(filesystem, escher::kFlatlandStandardProgram)) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
