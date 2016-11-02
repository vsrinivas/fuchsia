// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/processargs.h>
#include <mxio/util.h>

#include "apps/modular/lib/app/application.h"

namespace modular {

Application::Application(
    fidl::InterfaceHandle<ServiceProvider> environment_services,
    fidl::InterfaceRequest<ServiceProvider> outgoing_services)
    : environment_services_(
          ServiceProviderPtr::Create(std::move(environment_services))),
      outgoing_services_(std::move(outgoing_services)) {}

Application::~Application() = default;

std::unique_ptr<Application> Application::CreateFromStartupInfo() {
  mx_handle_t incoming = mxio_get_startup_handle(MX_HND_TYPE_INCOMING_SERVICES);
  mx_handle_t outgoing = mxio_get_startup_handle(MX_HND_TYPE_OUTGOING_SERVICES);

  return std::make_unique<Application>(
      fidl::InterfaceHandle<ServiceProvider>(mx::channel(incoming), 0u),
      fidl::InterfaceRequest<ServiceProvider>(mx::channel(outgoing)));
}

}  // namespace modular
