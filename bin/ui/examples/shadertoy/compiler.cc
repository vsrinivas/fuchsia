// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/shadertoy/compiler.h"

#include "lib/mtl/tasks/message_loop.h"

Compiler::Compiler() : loop_(mtl::MessageLoop::GetCurrent()) {}

Compiler::~Compiler() {
  std::lock_guard<std::mutex> lock(mutex_);
  while (!requests_.empty()) {
    requests_.front().callback({PipelinePtr()});
    requests_.pop();
  }
  if (has_thread_) {
    // TODO: This isn't a big deal, because it only happens when the process
    // is shutting down, but it would be tidier to wait for the thread to
    // finish.
    FTL_LOG(WARNING) << "Destroying while compile thread is still active.";
  }
}

void Compiler::Compile(std::string glsl, ResultCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!has_thread_) {
    thread_ = std::thread([this] { this->ProcessRequestQueue(); });
    has_thread_ = true;
  }
  requests_.push({std::move(glsl), std::move(callback)});
}

void Compiler::ProcessRequestQueue() {
  while (true) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (requests_.empty()) {
      thread_.detach();
      has_thread_ = false;
      return;
    }
    Request req(std::move(requests_.front()));
    requests_.pop();
    lock.unlock();

    loop_->task_runner()->PostTask([
      result = CreatePipeline(req.glsl), callback = std::move(req.callback)
    ] { callback(std::move(result)); });
  }
}

Compiler::Result Compiler::CreatePipeline(const std::string& glsl) {
  FTL_LOG(WARNING) << "Compiler::CreatePipeline() not implemented.";
  return Result{PipelinePtr()};
}
