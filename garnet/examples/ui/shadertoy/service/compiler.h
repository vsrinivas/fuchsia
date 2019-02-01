// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_COMPILER_H_
#define GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_COMPILER_H_

#include <functional>
#include <mutex>
#include <queue>
#include <thread>

#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <vulkan/vulkan.hpp>

#include "garnet/examples/ui/shadertoy/service/pipeline.h"
#include "lib/escher/escher.h"
#include "lib/escher/impl/model_data.h"

namespace shadertoy {

// The Shadertoy Compiler takes a GLSL source code fragment, transforms it by
// adding a header etc., compiles it, and generates a Pipeline that can be used
// by Renderer.  This is all done asynchronously; a callback is invoked when
// finished.
class Compiler final {
 public:
  // |render_pass| is not owned by us; we don't need to destroy it.
  explicit Compiler(async::Loop* loop, escher::EscherWeakPtr escher,
                    vk::RenderPass render_pass,
                    vk::DescriptorSetLayout descriptor_set_layout);
  ~Compiler();

  static const vk::DescriptorSetLayoutCreateInfo&
  GetDescriptorSetLayoutCreateInfo();

  // Result that is asynchronously returned by the Compiler.
  struct Result {
    PipelinePtr pipeline;
  };

  // Callback that is used to asynchronously notify clients of the result.
  using ResultCallback = fit::function<void(Result)>;

  // Compile GLSL source code on a background thread, and post a task to invoke
  // ResultCallback on the main thread.
  void Compile(std::string glsl, ResultCallback callback);

 private:
  struct Request {
    std::string glsl;
    ResultCallback callback;
  };

  PipelinePtr CompilePipeline(vk::ShaderModule vertex_module,
                              vk::ShaderModule fragment_module,
                              const escher::MeshSpec& mesh_spec);

  escher::impl::GlslToSpirvCompiler* glsl_compiler();

  // Drains the request queue in a background thread spawned by Compile().
  void ProcessRequestQueue();

  // Attempt to create a pipeline by compiling the provided GLSL code.
  // Invoked by ProcessRequestQueue().
  PipelinePtr CompileGlslToPipeline(const std::string& glsl_code);

  // Helper for CompileGlslToPipeline.
  PipelinePtr ConstructPipeline(vk::ShaderModule vertex_module,
                                vk::ShaderModule fragment_module,
                                const escher::MeshSpec& mesh_spec);

  async::Loop* const loop_;
  const escher::EscherWeakPtr escher_;
  escher::impl::ModelDataPtr model_data_;
  vk::RenderPass render_pass_;
  vk::DescriptorSetLayout descriptor_set_layout_;

  std::mutex mutex_;
  std::queue<Request> requests_;
  std::thread thread_;
  bool has_thread_ = false;
};

}  // namespace shadertoy

#endif  // GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_COMPILER_H_
