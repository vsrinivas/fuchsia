// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/service_provider_dir_impl.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/lib/fxl/strings/substitute.h"

namespace component {

namespace {
constexpr char kSandboxDocUrl[] =
    "https://fuchsia.dev/fuchsia-src/concepts/framework/sandboxing#services";

std::string ServiceNotInSandbox(const std::string& component_moniker,
                                const std::string& service_name) {
  return fxl::Substitute(
      "`$0` is not allowed to connect to `$1` because this service is not present in the "
      "component's sandbox.\nRefer to $2 for more information.",
      component_moniker, service_name, kSandboxDocUrl);
}

std::string ServiceNotAvailable(const std::string& component_moniker,
                                const std::string& service_name) {
  return fxl::Substitute(
      "`$0` could not connect to `$1` because this service is not present in the component's "
      "environment or additional services.",
      component_moniker, service_name);
}

std::string ErrorServingService(const std::string& component_moniker,
                                const std::string& service_name, zx_status_t status) {
  return fxl::Substitute(
      "`$0` could not connect to `$1`, because even though the service was present we encountered "
      "an error attempting to serve from it: $2",
      component_moniker, service_name, std::string(zx_status_get_string(status)));
}

}  // namespace

ServiceProviderDirImpl::ServiceProviderDirImpl(fbl::RefPtr<LogConnectorImpl> log_connector,
                                               const std::vector<std::string>* services)
    : vfs_(async_get_default_dispatcher()),
      root_(fbl::AdoptRef(new fs::PseudoDir())),
      log_connector_(log_connector),
      weak_factory_(this) {
  if (services != nullptr) {
    has_services_allowlist_ = true;
    services_allowlist_.insert(services->begin(), services->end());
  }
}

ServiceProviderDirImpl::~ServiceProviderDirImpl() {}

void ServiceProviderDirImpl::set_parent(fbl::RefPtr<ServiceProviderDirImpl> parent) {
  if (parent_) {
    return;
  }
  parent_ = parent;
  // Inherit the parent's services.
  for (const auto& s : parent_->service_handles_) {
    // Don't inherit the parent's LogSink if it was provided by appmgr because parent's LogSink
    // is private and attributed to itself. However, if parent's LogSink is custom (not provided
    // by appmgr), then it will be inherited.
    if (s.first == fuchsia::logger::LogSink::Name_ && parent->has_builtin_logsink_) {
      continue;
    }
    AddService(s.first, s.second);
  }
}

void ServiceProviderDirImpl::AddService(const std::string& service_name,
                                        fbl::RefPtr<fs::Service> service) {
  if (all_service_names_.count(service_name) > 0) {
    // Don't allow duplicate services. This path can be reached if a child
    // would inherit a service from its parent with a name that it already
    // has. In that case, the child's service should take priority.
    return;
  }
  if (IsServiceAllowlisted(service_name)) {
    service_handles_.push_back({service_name, service});
    root_->AddEntry(service_name, std::move(service));
    all_service_names_.insert(service_name);
  }
}

void ServiceProviderDirImpl::AddBinding(
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ServiceProviderDirImpl::ConnectToService(std::string service_name, zx::channel channel) {
  if (!IsServiceAllowlisted(service_name)) {
    FX_LOGS(WARNING) << ServiceNotInSandbox(component_moniker_, service_name);
    return;
  }
  fbl::RefPtr<fs::Vnode> child;
  zx_status_t status = root_->Lookup(service_name, &child);
  if (status == ZX_OK) {
    status = vfs_.Serve(child, std::move(channel), fs::VnodeConnectionOptions());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << ErrorServingService(component_moniker_, service_name, status);
    }
  } else {
    FX_LOGS(WARNING) << ServiceNotAvailable(component_moniker_, service_name);
  }
}

zx_status_t ServiceProviderDirImpl::GetAttributes(fs::VnodeAttributes* a) {
  return root_->GetAttributes(a);
}

zx_status_t ServiceProviderDirImpl::Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                                            size_t* out_actual) {
  return root_->Readdir(cookie, dirents, len, out_actual);
}

zx_status_t ServiceProviderDirImpl::GetNodeInfoForProtocol(
    [[maybe_unused]] fs::VnodeProtocol protocol, [[maybe_unused]] fs::Rights rights,
    fs::VnodeRepresentation* representation) {
  *representation = fs::VnodeRepresentation::Directory();
  return ZX_OK;
}

fs::VnodeProtocolSet ServiceProviderDirImpl::GetProtocols() const {
  return fs::VnodeProtocol::kDirectory;
}

zx_status_t ServiceProviderDirImpl::Lookup(fbl::StringPiece name, fbl::RefPtr<fs::Vnode>* out) {
  const std::string service_name(name.data(), name.length());
  if (!IsServiceAllowlisted(service_name)) {
    FX_LOGS(WARNING) << ServiceNotInSandbox(component_moniker_, service_name);
    return ZX_ERR_NOT_FOUND;
  }
  zx_status_t status = root_->Lookup(name, out);
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << ServiceNotAvailable(component_moniker_, service_name);
  }
  return status;
}

void ServiceProviderDirImpl::InitLogging() {
  // A log connector if they ask for it.
  if (IsServiceAllowlisted(fuchsia::sys::internal::LogConnector::Name_)) {
    AddService(
        fuchsia::sys::internal::LogConnector::Name_,
        fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          fidl::InterfaceRequest<fuchsia::sys::internal::LogConnector> request(std::move(channel));
          log_connector_->AddConnectorClient(std::move(request));
          return ZX_OK;
        })));
  }

  // If LogSink was allowlisted and wasn't explicitly provided to us, give it an attributed log
  // sink.
  if (all_service_names_.count(fuchsia::logger::LogSink::Name_) == 0 &&
      IsServiceAllowlisted(fuchsia::logger::LogSink::Name_)) {
    has_builtin_logsink_ = true;
    // Forward the LogSink request to the backing LogConnector, attributing it with
    AddService(
        fuchsia::logger::LogSink::Name_, fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          fidl::InterfaceRequest<fuchsia::logger::LogSink> request(std::move(channel));
          log_connector_->AddLogConnection(component_url(), component_id_, std::move(request));
          return ZX_OK;
        })));
  }
}

}  // namespace component
