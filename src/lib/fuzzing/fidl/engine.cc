// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "engine.h"

#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

using ::fuchsia::fuzzer::Coverage;
using ::fuchsia::fuzzer::DataProvider;
using ::fuchsia::fuzzer::LlvmFuzzerPtr;

// Public methods

Engine::Engine() {
  loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  context_ = sys::ComponentContext::Create();
  dispatcher_ = loop_->dispatcher();
  FX_CHECK(context_->outgoing()->ServeFromStartupInfo(loop_->dispatcher()) == ZX_OK);
  context_->outgoing()->AddPublicService(coverage_.GetHandler());
  context_->outgoing()->AddPublicService(data_provider_.GetHandler());
  loop_->StartThread();
}

Engine::Engine(std::unique_ptr<sys::ComponentContext> context, async_dispatcher_t* dispatcher)
    : context_(std::move(context)), dispatcher_(dispatcher) {
  context_->outgoing()->AddPublicService(coverage_.GetHandler());
  context_->outgoing()->AddPublicService(data_provider_.GetHandler());
}

Engine::~Engine() {
  if (loop_) {
    loop_->Shutdown();
  }
}

int Engine::RunOne(const uint8_t* data, size_t size) {
  if (!llvm_fuzzer_.is_bound()) {
    // TakeFuzzer blocks until the LlvmFuzzer handle is provided to the DataProvider.
    llvm_fuzzer_.Bind(data_provider_.TakeFuzzer(), dispatcher_);
  }

  zx_status_t status = data_provider_.PartitionTestInput(data, size);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to start iteration: " << zx_status_get_string(status);
    return status;
  }

  sync_completion_t sync;
  llvm_fuzzer_->TestOneInput([&status, &sync](int result) {
    status = result;
    sync_completion_signal(&sync);
  });

  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "fuzz target function returned non-zero status: " << status;
    return status;
  }

  if ((status = data_provider_.CompleteIteration()) != ZX_OK ||
      (status = coverage_.CompleteIteration()) != ZX_OK) {
    FX_LOGS(ERROR) << "failed to complete iteration: " << zx_status_get_string(status);
    return status;
  }
  return ZX_OK;
}

}  // namespace fuzzing
