// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/service_provider_dir_impl.h"

#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <zircon/status.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/substitute.h"

namespace component {

namespace {
constexpr char kSandboxDocUrl[] =
    "https://fuchsia.googlesource.com/fuchsia/+/master/docs/the-book/"
    "sandboxing.md#services";

std::string ServiceNotInSandbox(const std::string& component_url,
                                const std::string& service_name) {
  return fxl::Substitute(
      "Component $0 is not allowed to connect to $1 because this service "
      "is not present in the component's sandbox.\nRefer to $2 for more "
      "information.",
      component_url, service_name, kSandboxDocUrl);
}

std::string ErrorServingService(const std::string& component_url,
                                const std::string& service_name,
                                zx_status_t status) {
  return fxl::Substitute("Cannot serve service $0 for component $1: $2",
                         service_name, component_url,
                         std::string(zx_status_get_string(status)));
}

}  // namespace

ServiceProviderDirImpl::ServiceProviderDirImpl(
    const std::vector<std::string>* services)
    : vfs_(async_get_default_dispatcher()),
      root_(fbl::AdoptRef(new fs::PseudoDir())),
      weak_factory_(this) {
  if (services != nullptr) {
    has_services_whitelist_ = true;
    services_whitelist_.insert(services->begin(), services->end());
  }
}

ServiceProviderDirImpl::~ServiceProviderDirImpl() {}

void ServiceProviderDirImpl::set_parent(
    fbl::RefPtr<ServiceProviderDirImpl> parent) {
  if (parent_) {
    return;
  }
  parent_ = parent;
  for (const auto& s : parent_->service_handles_) {
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
  if (IsServiceWhitelisted(service_name)) {
    service_handles_.push_back({service_name, service});
    root_->AddEntry(service_name, std::move(service));
    all_service_names_.insert(service_name);
  }
}

void ServiceProviderDirImpl::AddBinding(
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ServiceProviderDirImpl::ConnectToService(std::string service_name,
                                              zx::channel channel) {
  if (!IsServiceWhitelisted(service_name)) {
    FXL_LOG(WARNING) << ServiceNotInSandbox(component_url_, service_name);
    return;
  }
  fbl::RefPtr<fs::Vnode> child;
  zx_status_t status = root_->Lookup(&child, service_name);
  if (status == ZX_OK) {
    status = child->Serve(&vfs_, std::move(channel), 0);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << ErrorServingService(component_url_, service_name,
                                            status);
    }
  } else {
    FXL_LOG(ERROR) << ErrorServingService(component_url_, service_name, status);
  }
}

bool ServiceProviderDirImpl::IsDirectory() const { return true; }

zx_status_t ServiceProviderDirImpl::Getattr(vnattr_t* a) {
  return root_->Getattr(a);
}

zx_status_t ServiceProviderDirImpl::Readdir(fs::vdircookie_t* cookie,
                                            void* dirents, size_t len,
                                            size_t* out_actual) {
  return root_->Readdir(cookie, dirents, len, out_actual);
}

zx_status_t ServiceProviderDirImpl::GetNodeInfo(uint32_t flags,
                                                fuchsia_io_NodeInfo* info) {
  info->tag = fuchsia_io_NodeInfoTag_directory;
  return ZX_OK;
}

zx_status_t ServiceProviderDirImpl::Lookup(fbl::RefPtr<fs::Vnode>* out,
                                           fbl::StringPiece name) {
  const std::string sname(name.data(), name.length());
  if (!IsServiceWhitelisted(sname)) {
    FXL_LOG(WARNING) << ServiceNotInSandbox(component_url_, sname);
  }
  return root_->Lookup(out, name);
}

}  // namespace component
