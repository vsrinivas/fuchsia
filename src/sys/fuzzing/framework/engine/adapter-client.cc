// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/adapter-client.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <string>
#include <vector>

namespace fuzzing {

TargetAdapterClient::TargetAdapterClient(fidl::InterfaceRequestHandler<TargetAdapter> handler)
    : handler_(std::move(handler)) {}

TargetAdapterClient::~TargetAdapterClient() { Close(); }

void TargetAdapterClient::AddDefaults(Options* options) {
  if (!options->has_max_input_size()) {
    options->set_max_input_size(kDefaultMaxInputSize);
  }
}

void TargetAdapterClient::Configure(const std::shared_ptr<Options>& options) {
  FX_CHECK(options);
  options_ = options;
  test_input_.Reserve(options_->max_input_size());
}

void TargetAdapterClient::Connect() {
  if (is_connected()) {
    return;
  }
  FX_CHECK(options_);
  handler_(adapter_.NewRequest());
  auto eventpair = coordinator_.Create([this](zx_signals_t observed) {
    sync_.Signal();
    // The only signal we expected to receive from the target adapter is |kFinish| after each run.
    return observed == kFinish;
  });
  auto status = adapter_->Connect(std::move(eventpair), test_input_.Share());
  FX_CHECK(status == ZX_OK) << "fuchsia.fuzzer.TargetAdapter.Connect: "
                            << zx_status_get_string(status);
}

std::vector<std::string> TargetAdapterClient::GetParameters() {
  std::vector<std::string> parameters;
  Connect();
  auto status = adapter_->GetParameters(&parameters);
  FX_CHECK(status == ZX_OK) << "TargetAdapter.GetParameters: " << zx_status_get_string(status);
  return parameters;
}

void TargetAdapterClient::Start(Input* test_input) {
  Connect();
  // Write the test input.
  test_input_.Clear();
  test_input_.Write(test_input->data(), test_input->size());
  // Signal the target adapter to start, unless this object is already an error state. The more
  // "natural" phrasing of "if not error, then reset and signal peer" has an inherent race where an
  // error may occur between the reset. The race is avoided by resetting first then "unresetting",
  // i.e. signalling, if there's a pending error.
  sync_.Reset();
  if (error_.load()) {
    sync_.Signal();
  } else {
    coordinator_.SignalPeer(kStart);
  }
}

void TargetAdapterClient::AwaitFinish() { sync_.WaitFor("target adapter to finish"); }

void TargetAdapterClient::SetError() {
  if (!error_.exchange(true)) {
    sync_.Signal();
  }
}

void TargetAdapterClient::ClearError() { error_ = false; }

void TargetAdapterClient::Close() {
  sync_.Signal();
  coordinator_.Reset();
}

}  // namespace fuzzing
