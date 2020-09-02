// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "engine.h"

#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <fbl/string_piece.h>

namespace fuzzing {
namespace {

using ::fuchsia::fuzzer::Coverage;
using ::fuchsia::fuzzer::DataProvider;
using ::fuchsia::fuzzer::LlvmFuzzerPtr;

// Usage message for direct invocation.
void Usage(const char *argv0) {
  std::cout << "usage: " << argv0 << " [options] [libFuzzer-options...]" << std::endl;
  std::cout << std::endl;
  std::cout << "options:" << std::endl;
  std::cout << "  -h|--help:         Print this message and exit." << std::endl;
  std::cout << "  --label=<label>:   Partition test input on data consumer label." << std::endl;
  std::cout << std::endl;
  std::cout << "Unrecognized items are passed to libFuzzer." << std::endl;
  exit(0);
}

// Stop the singleton when exiting.
void AtExit() { EngineImpl::GetInstance(false /* autoconnect */)->Stop(ZX_OK); }

}  // namespace

// Public methods

/* static */ EngineImpl *EngineImpl::GetInstance(bool autostart) {
  static EngineImpl engine(autostart);
  return &engine;
}

EngineImpl::~EngineImpl() {
  // Called on exit.
  Stop(ZX_OK);
}

zx_status_t EngineImpl::SetLlvmFuzzer(LlvmFuzzerPtr llvm_fuzzer) {
  // Set the pointer to the Coverage service.
  if (!llvm_fuzzer.is_bound()) {
    return ZX_ERR_INVALID_ARGS;
  }
  Stop(ZX_ERR_PEER_CLOSED);
  llvm_fuzzer_ = std::move(llvm_fuzzer);

  return ZX_OK;
}

void EngineImpl::Start(std::vector<std::string> options, StartCallback callback) {
  if (callback_) {
    FX_LOGS(ERROR) << "Already started.";
    callback(ZX_ERR_BAD_STATE);
    return;
  }
  if (!llvm_fuzzer_.is_bound()) {
    FX_LOGS(ERROR) << "LlvmFuzzer not set for engine.";
    callback(ZX_ERR_BAD_STATE);
    return;
  }
  callback_ = std::move(callback);

  zx::vmo vmo;
  data_provider_.Initialize(&vmo);

  llvm_fuzzer_->Initialize(std::move(vmo), std::move(options),
                           [this](int result, std::vector<std::string> modified) {
                             options_ = std::move(modified);
                             if (result != 0) {
                               Stop(result);
                             }
                             sync_completion_signal(&sync_);
                           });

  // libFuzzer's own std::atexit calls have run, so this will be the first exit callback invoked.
  std::atexit(::fuzzing::AtExit);
}

int EngineImpl::Initialize(int *argc, char ***argv) {
  if (!argv_.empty()) {
    FX_LOGS(ERROR) << "Already initialized.";
    return ZX_ERR_BAD_STATE;
  }

  // Extract the consumer labels.
  static const char *kLabel = "--label=";
  for (int i = 0; i < *argc; ++i) {
    std::string arg((*argv)[i]);
    if (arg == "-h" || arg == "--help") {
      Usage((*argv)[0]);
    } else if (arg.rfind(kLabel, 0) == 0) {
      data_provider_.AddConsumerLabel(arg.substr(fbl::constexpr_strlen(kLabel)));
    } else {
      argv_.push_back((*argv)[i]);
    }
  }

  // Wait until the engine is started, then append the libFuzzer options.
  sync_completion_wait(&sync_, ZX_TIME_INFINITE);
  std::transform(options_.begin(), options_.end(), std::back_inserter(argv_),
                 [](const std::string &s) { return const_cast<char *>(s.c_str()); });
  *argc = argv_.size();
  *argv = &argv_[0];

  return ZX_OK;
}

int EngineImpl::TestOneInput(const uint8_t *data, size_t size) {
  if (!sync_completion_signaled(&sync_)) {
    FX_LOGS(ERROR) << "Not initialized.";
    return ZX_ERR_BAD_STATE;
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

void EngineImpl::Stop(zx_status_t status) {
  coverage_.Reset();
  data_provider_.Reset();
  llvm_fuzzer_.Unbind();
  if (callback_) {
    callback_(status);
    callback_ = nullptr;
  }
  sync_completion_reset(&sync_);
}

// Private methods

EngineImpl::EngineImpl(bool autoconnect) {
  if (autoconnect) {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    FX_CHECK(context_->outgoing()->ServeFromStartupInfo(dispatcher_) == ZX_OK);
    context_->outgoing()->AddPublicService(coverage_.GetHandler());
    context_->outgoing()->AddPublicService(data_provider_.GetHandler());
    FX_CHECK(loop_->StartThread() == ZX_OK);
    auto svc = sys::ServiceDirectory::CreateFromNamespace();
    LlvmFuzzerPtr llvm_fuzzer;
    svc->Connect(llvm_fuzzer.NewRequest(loop_->dispatcher()));
    FX_CHECK(SetLlvmFuzzer(std::move(llvm_fuzzer)) == ZX_OK);
  }
}

void EngineImpl::UseContextImpl(std::unique_ptr<sys::ComponentContext> context) {
  Stop(ZX_OK);
  argv_.clear();
  context_ = std::move(context);
  context_->outgoing()->AddPublicService(coverage_.GetHandler());
  context_->outgoing()->AddPublicService(data_provider_.GetHandler());
}

}  // namespace fuzzing
