// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_SERVICE_PROVIDER_DIR_IMPL_H_
#define GARNET_BIN_APPMGR_SERVICE_PROVIDER_DIR_IMPL_H_

#include <string>
#include <unordered_set>

#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace component {

// A directory-like object which dynamically creates Service vnodes
// for any file lookup. It also exposes service provider interface.
//
// It supports enumeration for only first level of services.
class ServiceProviderDirImpl : public fuchsia::sys::ServiceProvider,
                               public fs::Vnode {
 public:
  ServiceProviderDirImpl();
  ~ServiceProviderDirImpl() override;

  void set_parent(fbl::RefPtr<ServiceProviderDirImpl> parent) {
    if (parent_) {
      return;
    }
    parent_ = parent;
    // invalidate backing_dir_;
    backing_dir_.reset();
  }

  void set_backing_dir(zx::channel backing_dir) {
    // only set if no parent.
    if (!parent_) {
      backing_dir_ = std::move(backing_dir);
    }
  }

  void AddService(fbl::RefPtr<fs::Service> service,
                  const std::string& service_name);

  void SetServicesWhitelist(const std::vector<std::string>& services);

  void AddBinding(
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request);

  // Overridden from |fs::Vnode|:
  zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) final;

  zx_status_t Getattr(vnattr_t* a) final;

  zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                      size_t* out_actual) final;

 private:
  // Overridden from |fuchsia::sys::ServiceProvider|:
  void ConnectToService(fidl::StringPtr service_name,
                        zx::channel channel) override;

  fidl::BindingSet<fuchsia::sys::ServiceProvider> bindings_;
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> root_;
  fbl::RefPtr<ServiceProviderDirImpl> parent_;
  zx::channel backing_dir_;
  fxl::WeakPtrFactory<ServiceProviderDirImpl> weak_factory_;
  // TODO(CP-25): Remove has_services_whitelist_ when empty services is
  // equivalent to no services.
  bool has_services_whitelist_ = false;
  std::unordered_set<std::string> services_whitelist_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceProviderDirImpl);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_SERVICE_PROVIDER_DIR_IMPL_H_
