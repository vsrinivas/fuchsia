// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/controller-provider.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

namespace fuzzing {

using ::fuchsia::fuzzer::RegistrySyncPtr;

ControllerProviderImpl::ControllerProviderImpl()
    : binding_(this),
      close_([this]() { CloseImpl(); }),
      interrupt_([this]() { InterruptImpl(); }),
      join_([this]() { JoinImpl(); }) {}

ControllerProviderImpl::~ControllerProviderImpl() {
  close_.Run();
  interrupt_.Run();
  join_.Run();
}

///////////////////////////////////////////////////////////////
// FIDL methods

void ControllerProviderImpl::Connect(fidl::InterfaceRequest<Controller> request,
                                     ConnectCallback callback) {
  controller_.Bind(std::move(request));
  callback();
}

void ControllerProviderImpl::Stop() { binding_.Unbind(); }

///////////////////////////////////////////////////////////////
// Run-related methods

zx_status_t ControllerProviderImpl::Run(std::unique_ptr<Runner> runner) {
  SetRunner(std::move(runner));
  zx::channel channel{zx_take_startup_handle(PA_HND(PA_USER0, 0))};
  Serve(std::move(channel));
  binding_.AwaitClose();
  return ZX_OK;
}

void ControllerProviderImpl::SetRunner(std::unique_ptr<Runner> runner) {
  FX_CHECK(runner);
  controller_.SetRunner(std::move(runner));
}

void ControllerProviderImpl::Serve(zx::channel channel) {
  FX_CHECK(channel);
  registrar_.Bind(std::move(channel));
  auto provider = binding_.NewBinding();
  registrar_->Register(std::move(provider));
}

///////////////////////////////////////////////////////////////
// Stop-related methods

void ControllerProviderImpl::CloseImpl() {
  binding_.Unbind();
  controller_.Close();
}

void ControllerProviderImpl::InterruptImpl() { controller_.Interrupt(); }

void ControllerProviderImpl::JoinImpl() { controller_.Join(); }

}  // namespace fuzzing
