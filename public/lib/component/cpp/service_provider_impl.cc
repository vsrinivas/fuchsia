// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/component/cpp/service_provider_impl.h"

#include <utility>

namespace component {

ServiceProviderImpl::ServiceProviderImpl() {}

ServiceProviderImpl::ServiceProviderImpl(
    fidl::InterfaceRequest<ServiceProvider> request) {
  AddBinding(std::move(request));
}

ServiceProviderImpl::~ServiceProviderImpl() = default;

void ServiceProviderImpl::AddBinding(
    fidl::InterfaceRequest<ServiceProvider> request) {
  if (request)
    bindings_.AddBinding(this, std::move(request));
}

void ServiceProviderImpl::Close() { bindings_.CloseAll(); }

void ServiceProviderImpl::AddServiceForName(ServiceConnector connector,
                                            const std::string& service_name) {
  name_to_service_connector_[service_name] = std::move(connector);
}

void ServiceProviderImpl::RemoveServiceForName(
    const std::string& service_name) {
  auto it = name_to_service_connector_.find(service_name);
  if (it != name_to_service_connector_.end())
    name_to_service_connector_.erase(it);
}

void ServiceProviderImpl::ConnectToService(fidl::StringPtr service_name,
                                           zx::channel client_handle) {
  auto it = name_to_service_connector_.find(service_name);
  if (it != name_to_service_connector_.end())
    it->second(std::move(client_handle));
  else if (default_service_connector_)
    default_service_connector_(service_name, std::move(client_handle));
}

void ServiceProviderImpl::SetDefaultServiceConnector(
    DefaultServiceConnector connector) {
  default_service_connector_ = std::move(connector);
}

void ServiceProviderImpl::SetDefaultServiceProvider(
    fuchsia::sys::ServiceProviderPtr provider) {
  if (!provider) {
    default_service_connector_ = DefaultServiceConnector();
    return;
  }

  default_service_connector_ = [provider = std::move(provider)](
                                   std::string service_name,
                                   zx::channel client_handle) {
    provider->ConnectToService(service_name, std::move(client_handle));
  };
}

}  // namespace component
