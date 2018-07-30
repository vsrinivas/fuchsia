// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/service_provider_dir_impl.h"

#include <lib/async/default.h>
#include <lib/fdio/util.h>
#include "lib/fxl/logging.h"

namespace component {

ServiceProviderDirImpl::ServiceProviderDirImpl()
    : vfs_(async_get_default_dispatcher()),
      root_(fbl::AdoptRef(new fs::PseudoDir())),
      weak_factory_(this) {}

ServiceProviderDirImpl::~ServiceProviderDirImpl() {}

void ServiceProviderDirImpl::AddService(fbl::RefPtr<fs::Service> service,
                                        const std::string& service_name) {
  root_->AddEntry(service_name, std::move(service));
}

void ServiceProviderDirImpl::SetServicesWhitelist(
    const std::vector<std::string>& services) {
  has_services_whitelist_ = true;
  services_whitelist_.insert(services.begin(), services.end());
}

void ServiceProviderDirImpl::AddBinding(
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ServiceProviderDirImpl::ConnectToService(fidl::StringPtr service_name,
                                              zx::channel channel) {
  if (has_services_whitelist_ &&
      services_whitelist_.count(service_name.get()) == 0) {
    FXL_LOG(WARNING) << "Component is not allowed to connect to "
                     << service_name.get();
    return;
  }
  fbl::RefPtr<fs::Vnode> child;
  zx_status_t status = root_->Lookup(&child, service_name.get());
  if (status == ZX_OK) {
    status = child->Serve(&vfs_, std::move(channel), 0);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Could not serve " << service_name << ": " << status;
    }
  } else if (parent_) {
    parent_->ConnectToService(std::move(service_name), std::move(channel));
  } else if (backing_dir_) {
    fdio_service_connect_at(backing_dir_.get(), service_name->c_str(),
                            channel.release());
  }
}

zx_status_t ServiceProviderDirImpl::Getattr(vnattr_t* a) {
  return root_->Getattr(a);
}

zx_status_t ServiceProviderDirImpl::Readdir(fs::vdircookie_t* cookie,
                                            void* dirents, size_t len,
                                            size_t* out_actual) {
  // TODO(CP-25): Filter services according to the whitelist. In general,
  // fix readdir so that it returns all services, not just the ones under root_.
  return root_->Readdir(cookie, dirents, len, out_actual);
}

zx_status_t ServiceProviderDirImpl::Lookup(fbl::RefPtr<fs::Vnode>* out,
                                           fbl::StringPiece name) {
  *out = fbl::AdoptRef(
      new fs::Service([name = std::string(name.data(), name.length()),
                       ptr = weak_factory_.GetWeakPtr()](zx::channel channel) {
        if (ptr) {
          ptr->ConnectToService(name, std::move(channel));
          return ZX_OK;
        }
        return ZX_ERR_NOT_FOUND;
      }));
  return ZX_OK;
}

}  // namespace component
