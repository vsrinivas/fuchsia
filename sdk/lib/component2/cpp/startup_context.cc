// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/component2/cpp/startup_context.h>

#include <lib/fdio/directory.h>

namespace component2 {

StartupContext::StartupContext(zx::channel service_root,
                               zx::channel directory_request,
                               async_dispatcher_t* dispatcher)
    : service_root_(std::move(service_root)) {
  outgoing_.Serve(std::move(directory_request), dispatcher);
}

StartupContext::~StartupContext() = default;

std::unique_ptr<StartupContext> StartupContext::CreateFromStartupInfo() {
  // TODO: Implement.
  return nullptr;
}

std::unique_ptr<StartupContext> StartupContext::CreateFrom(
    fuchsia::sys::StartupInfo startup_info) {
  // TODO: Implement.
  return nullptr;
}

void StartupContext::Connect(const std::string& interface_name,
                             zx::channel channel) {
  fdio_service_connect_at(service_root_.get(), interface_name.c_str(),
                          channel.release());
}

}  // namespace component2
