// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/svc/cpp/service_namespace.h"

#include <fcntl.h>
#include <fs/service.h>
#include <lib/async/default.h>
#include <lib/fdio/util.h>
#include <zircon/device/vfs.h>

#include <utility>

#include "lib/fxl/files/unique_fd.h"

namespace component {

ServiceNamespace::ServiceNamespace()
    : directory_(fbl::AdoptRef(new fs::PseudoDir())) {}

ServiceNamespace::ServiceNamespace(
    fidl::InterfaceRequest<ServiceProvider> request)
    : ServiceNamespace() {
  AddBinding(std::move(request));
}

ServiceNamespace::ServiceNamespace(fbl::RefPtr<fs::PseudoDir> directory)
    : directory_(std::move(directory)) {}

ServiceNamespace::~ServiceNamespace() = default;

void ServiceNamespace::AddBinding(
    fidl::InterfaceRequest<ServiceProvider> request) {
  if (request)
    bindings_.AddBinding(this, std::move(request));
}

void ServiceNamespace::Close() { bindings_.CloseAll(); }

void ServiceNamespace::AddServiceForName(ServiceConnector connector,
                                         const std::string& service_name) {
  name_to_service_connector_[service_name] = std::move(connector);
  directory_->AddEntry(
      service_name,
      fbl::AdoptRef(new fs::Service([this, service_name](zx::channel channel) {
        ConnectCommon(service_name, std::move(channel));
        return ZX_OK;
      })));
}

void ServiceNamespace::RemoveServiceForName(const std::string& service_name) {
  auto it = name_to_service_connector_.find(service_name);
  if (it != name_to_service_connector_.end())
    name_to_service_connector_.erase(it);
  directory_->RemoveEntry(service_name);
}

void ServiceNamespace::Connect(fbl::StringPiece name, zx::channel channel) {
  ConnectCommon(std::string(name.data(), name.length()), std::move(channel));
}

void ServiceNamespace::ConnectToService(fidl::StringPtr service_name,
                                        zx::channel channel) {
  ConnectCommon(service_name, std::move(channel));
}

void ServiceNamespace::ConnectCommon(const std::string& service_name,
                                     zx::channel channel) {
  auto it = name_to_service_connector_.find(service_name);
  if (it != name_to_service_connector_.end())
    it->second(std::move(channel));
}

}  // namespace component
