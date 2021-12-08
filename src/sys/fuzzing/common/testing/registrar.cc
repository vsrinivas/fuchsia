// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/registrar.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

FakeRegistrar::FakeRegistrar() : binding_(this) {}

zx::channel FakeRegistrar::Bind() {
  zx::channel client, server;
  auto status = zx::channel::create(0, &client, &server);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  binding_.Bind(std::move(server));
  return client;
}

void FakeRegistrar::Register(fidl::InterfaceHandle<ControllerProvider> provider,
                             RegisterCallback callback) {
  provider_ = std::move(provider);
  callback();
}

fidl::InterfaceHandle<ControllerProvider> FakeRegistrar::TakeProvider() {
  return std::move(provider_);
}

}  // namespace fuzzing
