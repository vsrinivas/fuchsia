// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/component-context.h"

#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

namespace fuzzing {

ComponentContextPtr ComponentContextForTest::Create() {
  auto sys_context = sys::ComponentContext::Create();
  auto loop = std::make_unique<fidl::test::AsyncLoopForTest>();
  auto executor = MakeExecutor(loop->dispatcher());
  auto svc = sys_context->svc();

  auto context = std::make_unique<ComponentContextForTest>();
  context->loop_ = std::move(loop);
  context->set_executor(std::move(executor));
  context->set_svc(std::move(svc));
  return context;
}

ComponentContextPtr ComponentContextForTest::Create(ExecutorPtr executor) {
  auto sys_context = sys::ComponentContext::Create();
  auto svc = sys_context->svc();

  auto context = std::make_unique<ComponentContextForTest>();
  context->set_executor(std::move(executor));
  context->set_svc(std::move(svc));
  return context;
}

// Adds a channel as if it had been passed as the |PA_HND(PA_USER0, arg)| startup handle.
void ComponentContextForTest::PutChannel(uint32_t arg, zx::channel channel) {
  channels_[arg] = std::move(channel);
}

// If |PutChannel| was called with the given |arg|, returns that channel; otherwise, returns an
// invalid channel.
zx::channel ComponentContextForTest::TakeChannel(uint32_t arg) {
  auto iter = channels_.find(arg);
  if (iter == channels_.end()) {
    return zx::channel();
  }
  auto channel = std::move(iter->second);
  channels_.erase(iter);
  return channel;
}

zx_status_t ComponentContextForTest::Run() { return RunUntilIdle(); }

zx_status_t ComponentContextForTest::RunUntilIdle() {
  if (loop_) {
    return loop_->RunUntilIdle();
  }
  return ComponentContext::RunUntilIdle();
}

}  // namespace fuzzing
