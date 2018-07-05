// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_GLSL_COMPILER_H_
#define LIB_ESCHER_IMPL_GLSL_COMPILER_H_

#include <future>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace escher {
namespace impl {

typedef std::vector<uint32_t> SpirvData;

// Wraps the reference GLSL compiler provided by Khronos.
// TODO: GLSL standard library functions are currently not available.
class GlslToSpirvCompiler {
 public:
  GlslToSpirvCompiler();
  ~GlslToSpirvCompiler();

  // Compile and link the provided source code snippets into a single SPIR-V
  // binary, which is returned as a string.  |preamble| and |entry_point| may be
  // empty strings.  If an error is encountered during compilation, an empty
  // string is returned.
  std::future<SpirvData> Compile(vk::ShaderStageFlagBits stage,
                                 std::vector<std::string> glsl_source_code,
                                 std::string preamble, std::string entry_point);

 private:
  // Same as Compile(), but completes synchronously.
  SpirvData SynchronousCompile(vk::ShaderStageFlagBits stage,
                               std::vector<std::string> glsl_source_code,
                               std::string preamble, std::string entry_point);

  // Helper for SynchronousCompile.
  SpirvData SynchronousCompileImpl(vk::ShaderStageFlagBits stage,
                                   std::vector<std::string> glsl_source_code,
                                   std::string preamble,
                                   std::string entry_point);

  std::atomic<uint32_t> active_compile_count_;
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_GLSL_COMPILER_H_
