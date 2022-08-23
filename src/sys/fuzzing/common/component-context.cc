// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/component-context.h"

#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

namespace fuzzing {

ComponentContextPtr ComponentContext::Create() {
  static bool once = true;
  FX_CHECK(once) << "ComponentContext::Create called more than once.";
  once = false;

  auto sys_context = sys::ComponentContext::Create();
  auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
  auto executor = MakeExecutor(loop->dispatcher());
  auto svc = sys_context->svc();
  auto outgoing = sys_context->outgoing();

  auto context = std::make_unique<ComponentContext>();
  context->loop_ = std::move(loop);
  context->executor_ = std::move(executor);
  context->svc_ = std::move(svc);
  context->outgoing_ = std::move(outgoing);
  return context;
}

ComponentContextPtr ComponentContext::CreateAuxillary() {
  auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto executor = MakeExecutor(loop->dispatcher());
  auto svc = sys::ServiceDirectory::CreateFromNamespace();

  auto context = std::make_unique<ComponentContext>();
  context->loop_ = std::move(loop);
  context->executor_ = std::move(executor);
  context->svc_ = std::move(svc);
  return context;
}

ComponentContext::~ComponentContext() {
  if (loop_ && !outgoing_) {
    // Auxiliary context.
    loop_->Shutdown();
    loop_->JoinThreads();
  }
}

zx::channel ComponentContext::TakeChannel(uint32_t arg) {
  return zx::channel(zx_take_startup_handle(PA_HND(PA_USER0, arg)));
}

// Runs the message loop on the current thread. This method should only be called at most once.
zx_status_t ComponentContext::Run() {
  FX_CHECK(loop_);
  if (!outgoing_) {
    // Auxiliary context.
    return loop_->StartThread();
  }
  outgoing_->ServeFromStartupInfo(loop_->dispatcher());
  return loop_->Run();
}

zx_status_t ComponentContext::RunUntilIdle() {
  FX_CHECK(loop_);
  return loop_->RunUntilIdle();
}

}  // namespace fuzzing
