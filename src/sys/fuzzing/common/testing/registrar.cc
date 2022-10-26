// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/registrar.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

namespace fuzzing {

const char* kFakeFuzzerUrl = "fuchsia-pkg://fuchsia.com/fuzzing-common-tests#meta/fake.cm";

FakeRegistrar::FakeRegistrar(ExecutorPtr executor)
    : binding_(this), executor_(std::move(executor)), receiver_(&sender_) {}

fidl::InterfaceHandle<Registrar> FakeRegistrar::NewBinding() {
  auto handle = binding_.NewBinding(executor_->dispatcher());
  FX_DCHECK(handle.is_valid());
  return handle;
}

void FakeRegistrar::Register(std::string url, ControllerProviderHandle provider,
                             RegisterCallback callback) {
  auto status = sender_.Send(std::move(provider));
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  callback();
}

ZxPromise<ControllerProviderHandle> FakeRegistrar::TakeProvider() {
  return receiver_.Receive().or_else([] { return fpromise::error(ZX_ERR_CANCELED); });
}

}  // namespace fuzzing
