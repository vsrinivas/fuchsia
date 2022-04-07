// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/adapter-client.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

TargetAdapterClient::TargetAdapterClient(ExecutorPtr executor)
    : executor_(executor), eventpair_(executor) {}

void TargetAdapterClient::AddDefaults(Options* options) {
  if (!options->has_max_input_size()) {
    options->set_max_input_size(kDefaultMaxInputSize);
  }
}

void TargetAdapterClient::Configure(const OptionsPtr& options) {
  FX_CHECK(options);
  test_input_.Reserve(options->max_input_size());
}

Promise<> TargetAdapterClient::Connect() {
  return fpromise::make_promise([this, handling = Future<>(),
                                 connect = Future<>()](Context& context) mutable -> Result<> {
           if (!connect) {
             if (eventpair_.IsConnected()) {
               return fpromise::ok();
             }
             handler_(ptr_.NewRequest(executor_->dispatcher()));

             ptr_.set_error_handler([](zx_status_t status) {});

             Bridge<> bridge;
             ptr_->Connect(eventpair_.Create(), test_input_.Share(), bridge.completer.bind());
             connect = bridge.consumer.promise_or(fpromise::error());
           }
           if (!connect(context)) {
             return fpromise::pending();
           }
           return connect.result();
         })
      .wrap_with(scope_);
}

Promise<std::vector<std::string>> TargetAdapterClient::GetParameters() {
  return Connect()
      .and_then([this] {
        Bridge<std::vector<std::string>> bridge;
        ptr_->GetParameters(bridge.completer.bind());
        return bridge.consumer.promise_or(fpromise::error());
      })
      .wrap_with(scope_);
}

std::vector<std::string> TargetAdapterClient::GetSeedCorpusDirectories(
    const std::vector<std::string>& parameters) {
  std::vector<std::string> seed_corpus_dirs;
  bool ignored = false;
  std::copy_if(parameters.begin(), parameters.end(), std::back_inserter(seed_corpus_dirs),
               [&ignored](const std::string& parameter) {
                 ignored |= parameter == "--";
                 return !ignored && !parameter.empty() && parameter[0] != '-';
               });
  return seed_corpus_dirs;
}

Promise<> TargetAdapterClient::TestOneInput(const Input& test_input) {
  FX_CHECK(test_input_.size() == 0);
  test_input_.Write(test_input.data(), test_input.size());
  return Connect()
      .inspect([](const Result<>& result) {})
      .or_else([] { return fpromise::error(ZX_ERR_CANCELED); })
      .and_then([this]() -> ZxResult<> { return AsZxResult(eventpair_.SignalSelf(kFinish, 0)); })
      .and_then([this]() -> ZxResult<> { return AsZxResult(eventpair_.SignalPeer(0, kStart)); })
      .and_then(eventpair_.WaitFor(kFinish))
      .and_then([](const zx_signals_t& observed) -> ZxResult<> { return fpromise::ok(); })
      .or_else([](const zx_status_t& status) -> Result<> {
        if (status != ZX_ERR_PEER_CLOSED) {
          FX_LOGS(ERROR) << "Target adapter returned error: " << zx_status_get_string(status);
          return fpromise::error();
        }
        return fpromise::ok();
      })
      .inspect([this](const Result<>& result) { test_input_.Clear(); })
      .wrap_with(scope_);
}

void TargetAdapterClient::Clear() { test_input_.Clear(); }

void TargetAdapterClient::Disconnect() {
  eventpair_.Reset();
  ptr_.Unbind();
}

}  // namespace fuzzing
