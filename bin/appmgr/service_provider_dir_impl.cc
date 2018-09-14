// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/service_provider_dir_impl.h"

#include <lib/async/default.h>
#include <lib/fdio/util.h>
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/substitute.h"

namespace component {

namespace {
constexpr char kSandboxDocUrl[] =
    "https://fuchsia.googlesource.com/docs/+/master/the-book/"
    "sandboxing.md#services";

std::string ServiceNotInSandbox(const std::string& component_url,
                                const std::string& service_name) {
  return fxl::Substitute(
      "Component $0 is not allowed to connect to $1 because this service "
      "is not present in the component's sandbox.\nRefer to $2 for more "
      "information.",
      component_url, service_name, kSandboxDocUrl);
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
  // invalidate backing_dir_;
  backing_dir_.reset();
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

void ServiceProviderDirImpl::ConnectToService(fidl::StringPtr service_name,
                                              zx::channel channel) {
  if (!IsServiceWhitelisted(service_name.get())) {
    FXL_LOG(WARNING) << ServiceNotInSandbox(component_url_, service_name.get());
    return;
  }
  fbl::RefPtr<fs::Vnode> child;
  zx_status_t status = root_->Lookup(&child, service_name.get());
  if (status == ZX_OK) {
    status = child->Serve(&vfs_, std::move(channel), 0);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Could not serve " << service_name << ": " << status;
    }
  } else if (parent_ && parent_->backing_dir_) {
    fdio_service_connect_at(parent_->backing_dir_.get(), service_name->c_str(),
                            channel.release());
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
  return root_->Readdir(cookie, dirents, len, out_actual);
}

zx_status_t ServiceProviderDirImpl::Lookup(fbl::RefPtr<fs::Vnode>* out,
                                           fbl::StringPiece name) {
  const std::string sname(name.data(), name.length());
  if (backing_dir_ || (parent_ && parent_->backing_dir_)) {
    // Legacy behavior -- return a service, even though it might not actually
    // exist (there is no good way to forward the lookup to backing_dir_).
    // TODO(CP-124): Remove this when we remove support for backing_dir_.
    *out = fbl::AdoptRef(new fs::Service(
        [sname, ptr = weak_factory_.GetWeakPtr()](zx::channel channel) {
          if (ptr) {
            ptr->ConnectToService(sname, std::move(channel));
            return ZX_OK;
          }
          return ZX_ERR_NOT_FOUND;
        }));
    return ZX_OK;
  }

  if (!IsServiceWhitelisted(sname)) {
    FXL_LOG(WARNING) << ServiceNotInSandbox(component_url_, sname);
  }
  return root_->Lookup(out, name);
}

}  // namespace component
