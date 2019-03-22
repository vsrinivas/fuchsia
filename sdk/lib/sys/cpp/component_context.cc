// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/component_context.h>

#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

namespace sys {
namespace {

constexpr char kServiceRootPath[] = "/svc";

}  // namespace

ComponentContext::ComponentContext(std::shared_ptr<ServiceDirectory> svc,
                                   zx::channel directory_request,
                                   async_dispatcher_t* dispatcher)
    : svc_(std::move(svc)) {
  outgoing_.Serve(std::move(directory_request), dispatcher);
}

ComponentContext::~ComponentContext() = default;

std::unique_ptr<ComponentContext> ComponentContext::Create() {
  zx_handle_t directory_request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);
  return std::make_unique<ComponentContext>(
      ServiceDirectory::CreateFromNamespace(), zx::channel(directory_request));
}

std::unique_ptr<ComponentContext> ComponentContext::CreateFrom(
    fuchsia::sys::StartupInfo startup_info) {
  fuchsia::sys::FlatNamespace& flat = startup_info.flat_namespace;
  if (flat.paths.size() != flat.directories.size())
    return nullptr;

  zx::channel service_root;
  for (size_t i = 0; i < flat.paths.size(); ++i) {
    if (flat.paths.at(i) == kServiceRootPath) {
      service_root = std::move(flat.directories.at(i));
      break;
    }
  }

  return std::make_unique<ComponentContext>(
      std::make_shared<ServiceDirectory>(std::move(service_root)),
      std::move(startup_info.launch_info.directory_request));
}

}  // namespace sys
