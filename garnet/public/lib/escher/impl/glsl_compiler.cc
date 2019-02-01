// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/glsl_compiler.h"

#include <string>
#include <thread>

#include "SPIRV/GlslangToSpv.h"
#include "StandAlone/ResourceLimits.h"
#include "glslang/Public/ShaderLang.h"
#include "spirv-tools/libspirv.hpp"
#include "spirv-tools/optimizer.hpp"

#include "lib/escher/util/trace_macros.h"
#include "lib/fxl/logging.h"

namespace escher {
namespace impl {

GlslToSpirvCompiler::GlslToSpirvCompiler() : active_compile_count_(0) {}

GlslToSpirvCompiler::~GlslToSpirvCompiler() {
  FXL_CHECK(active_compile_count_ == 0);
}

std::future<SpirvData> GlslToSpirvCompiler::Compile(
    vk::ShaderStageFlagBits stage, std::vector<std::string> source_code,
    std::string preamble, std::string entry_point) {
  // Count will be decremented by SynchronousCompile.
  ++active_compile_count_;
#if !defined(ESCHER_DISABLE_BACKGROUND_COMPILATION)
  return std::async(
      std::launch::async, &GlslToSpirvCompiler::SynchronousCompile, this, stage,
      std::move(source_code), std::move(preamble), std::move(entry_point));
#else
  std::promise<SpirvData> p;
  p.set_value(SynchronousCompile(stage, std::move(source_code),
                                 std::move(preamble), std::move(entry_point)));
  return p.get_future();
#endif
}

SpirvData GlslToSpirvCompiler::SynchronousCompile(
    vk::ShaderStageFlagBits stage, std::vector<std::string> source_code,
    std::string preamble, std::string entry_point) {
  TRACE_DURATION("gfx", "escher::GlslToSpirvCompiler::SynchronousCompile");

  // SynchronousCompileImpl has many return points; wrap it so that we don't
  // forget to --active_compile_count_ at one of them.
  auto result =
      SynchronousCompileImpl(stage, std::move(source_code), std::move(preamble),
                             std::move(entry_point));
  // Count was already incremented by Compile().
  --active_compile_count_;
  return result;
}

// SynchronousCompileImpl has many return points; wrap it so that we don't
// forget to --active_compile_count_ at one of them.
SpirvData GlslToSpirvCompiler::SynchronousCompileImpl(
    vk::ShaderStageFlagBits stage_in, std::vector<std::string> source_code,
    std::string preamble, std::string entry_point) {
  EShLanguage stage;
  switch (stage_in) {
    case vk::ShaderStageFlagBits::eVertex:
      stage = EShLangVertex;
      break;
    case vk::ShaderStageFlagBits::eTessellationControl:
      stage = EShLangTessControl;
      break;
    case vk::ShaderStageFlagBits::eTessellationEvaluation:
      stage = EShLangTessEvaluation;
      break;
    case vk::ShaderStageFlagBits::eGeometry:
      stage = EShLangGeometry;
      break;
    case vk::ShaderStageFlagBits::eFragment:
      stage = EShLangFragment;
      break;
    case vk::ShaderStageFlagBits::eCompute:
      stage = EShLangCompute;
      break;
    default:
      FXL_LOG(WARNING) << "invalid shader stage";
      return SpirvData();
  }

  glslang::TShader shader(stage);

  std::vector<const char*> source_chars;
  std::vector<int> source_lengths;
  for (auto& s : source_code) {
    source_chars.push_back(s.c_str());
    source_lengths.push_back(s.length());
  }
  shader.setStringsWithLengths(source_chars.data(), source_lengths.data(),
                               source_code.size());
  if (!preamble.empty()) {
    shader.setPreamble(preamble.c_str());
  }
  if (!entry_point.empty()) {
    shader.setEntryPoint(entry_point.c_str());
  }

  constexpr int kDefaultGlslVersion = 450;
  constexpr EShMessages kMessageFlags =
      static_cast<EShMessages>(EShMsgVulkanRules | EShMsgSpvRules);

  if (!shader.parse(&glslang::DefaultTBuiltInResource, kDefaultGlslVersion,
                    false, kMessageFlags)) {
    FXL_LOG(WARNING) << "failed to parse shader \n\tinfo log: "
                     << shader.getInfoLog()
                     << "\n\tdebug log: " << shader.getInfoDebugLog();
    return SpirvData();
  }

  glslang::TProgram program;
  program.addShader(&shader);
  if (!program.link(kMessageFlags)) {
    FXL_LOG(WARNING) << "failed to link program \n\tinfo log: "
                     << program.getInfoLog()
                     << "\n\tdebug log: " << program.getInfoDebugLog();
    return SpirvData();
  }

  glslang::TIntermediate* intermediate = program.getIntermediate(stage);
  if (!intermediate) {
    FXL_LOG(WARNING) << "failed to obtain program intermediate representation"
                     << "\n\tinfo log : " << program.getInfoLog()
                     << "\n\tdebug log: " << program.getInfoDebugLog();
    return SpirvData();
  }

  SpirvData spirv;
  std::string warningsErrors;
  spv::SpvBuildLogger logger;
  glslang::GlslangToSpv(*intermediate, spirv, &logger);
  if (spirv.empty()) {
    FXL_LOG(WARNING) << "failed to generate SPIR-V from GLSL IR: "
                     << logger.getAllMessages();
  }

  // TODO(ES-24): Find a central place for all code that makes reference to a
  // particular version of Vulkan.
  const char* kVulkanMinorVersion = "42";

  spvtools::Optimizer optimizer(SPV_ENV_VULKAN_1_0);
  optimizer.RegisterPass(spvtools::CreateSetSpecConstantDefaultValuePass(
      {{1, kVulkanMinorVersion}}));
  optimizer.RegisterPass(spvtools::CreateFreezeSpecConstantValuePass());
  optimizer.RegisterPass(spvtools::CreateFoldSpecConstantOpAndCompositePass());
  optimizer.RegisterPass(spvtools::CreateEliminateDeadConstantPass());
  optimizer.RegisterPass(spvtools::CreateUnifyConstantPass());
  auto optimizer_error_logger = [](spv_message_level_t, const char*,
                                   const spv_position_t&, const char* m) {
    FXL_LOG(WARNING) << "Error while running SPIR-V optimizer: " << m;
  };
  optimizer.SetMessageConsumer(optimizer_error_logger);

  if (!optimizer.Run(spirv.data(), spirv.size(), &spirv)) {
    FXL_LOG(WARNING) << "failed to optimize SPIR-V";
    return SpirvData();
  }
  return spirv;
}

}  // namespace impl
}  // namespace escher
