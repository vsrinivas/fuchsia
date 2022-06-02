// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/registrar.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

namespace fuzzing {

FakeRegistrar::FakeRegistrar(ExecutorPtr executor)
    : binding_(this), executor_(std::move(executor)) {}

zx::channel FakeRegistrar::Bind() {
  zx::channel client, server;
  auto status = zx::channel::create(0, &client, &server);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  binding_.Bind(std::move(server), executor_->dispatcher());
  return client;
}

void FakeRegistrar::Register(ControllerProviderHandle provider, RegisterCallback callback) {
  auto status = providers_.Send(std::move(provider));
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  callback();
}

ZxPromise<ControllerProviderHandle> FakeRegistrar::TakeProvider() {
  return providers_.Receive().or_else([] { return fpromise::error(ZX_ERR_CANCELED); });
}

}  // namespace fuzzing
