// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vulkan/vulkan.hpp>

#include "escher/impl/glsl_compiler.h"

#include "pipeline.h"

namespace mtl {
class MessageLoop;
}

// The Shadertoy Compiler takes a GLSL source code fragment, transforms it by
// adding a header etc., compiles it, and generates a Pipeline that can be used
// by Renderer.  This is all done asynchronously; a callback is invoked when
// finished.
class Compiler final {
 public:
  Compiler();
  ~Compiler();

  // Result that is asynchronously returned by the Compiler.
  struct Result {
    PipelinePtr pipeline;
  };

  // Callback that is used to asynchronously notify clients of the result.
  using ResultCallback = std::function<void(Result)>;

  // Compile GLSL source code on a background thread, and post a task to invoke
  // ResultCallback on the main thread.
  void Compile(std::string glsl, ResultCallback callback);

 private:
  struct Request {
    std::string glsl;
    ResultCallback callback;
  };

  // Drains the request queue in a background thread spawned by Compile().
  void ProcessRequestQueue();

  // Attempt to create a pipeline by compiling the provided GLSL code.
  Result CreatePipeline(const std::string& glsl);

  mtl::MessageLoop* const loop_;
  escher::impl::GlslToSpirvCompiler compiler_;
  std::mutex mutex_;
  std::queue<Request> requests_;
  std::thread thread_;
  bool has_thread_ = false;
};
