// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/controller-provider.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

namespace fuzzing {

using ::fuchsia::fuzzer::RegistrySyncPtr;

ControllerProviderImpl::ControllerProviderImpl(ExecutorPtr executor)
    : binding_(this), controller_(std::move(executor)) {
  binding_.set_error_handler([](zx_status_t status) {
    // The registry signals the provider should exit by closing its channel.
    exit(0);
  });
}

///////////////////////////////////////////////////////////////
// FIDL methods

void ControllerProviderImpl::Connect(fidl::InterfaceRequest<Controller> request,
                                     ConnectCallback callback) {
  controller_.Bind(std::move(request));
  callback();
}

void ControllerProviderImpl::Stop() { controller_.Stop(); }

///////////////////////////////////////////////////////////////
// Run-related methods

Promise<> ControllerProviderImpl::Run(RunnerPtr runner) {
  SetRunner(std::move(runner));
  zx::channel channel{zx_take_startup_handle(PA_HND(PA_USER0, 0))};
  return Serve(std::move(channel));
}

void ControllerProviderImpl::SetRunner(RunnerPtr runner) {
  FX_CHECK(runner);
  controller_.SetRunner(std::move(runner));
}

Promise<> ControllerProviderImpl::Serve(zx::channel channel) {
  FX_CHECK(channel);
  registrar_.Bind(std::move(channel));
  auto provider = binding_.NewBinding();
  Bridge<> bridge;
  registrar_->Register(std::move(provider), bridge.completer.bind());
  return bridge.consumer.promise_or(fpromise::error());
}

}  // namespace fuzzing
