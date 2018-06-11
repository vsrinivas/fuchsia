// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_SERVICE_PROVIDER_DIR_IMPL_H_
#define GARNET_BIN_APPMGR_SERVICE_PROVIDER_DIR_IMPL_H_

#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/macros.h"

namespace fuchsia {
namespace sys {

// A directory-like object which dynamically creates Service vnodes
// for any file lookup. It also exposes service provider interface.
//
//  TODO(anmittal): Support enumeration for first level of services.
class ServiceProviderDirImpl : public ServiceProvider, public fs::Vnode {
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

  void AddBinding(fidl::InterfaceRequest<ServiceProvider> request);

  // Overridden from |fs::Vnode|:
  zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) final;

  zx_status_t Getattr(vnattr_t* a) final;

 private:
  // Overridden from |ServiceProvider|:
  void ConnectToService(fidl::StringPtr service_name,
                        zx::channel channel) override;

  fidl::BindingSet<ServiceProvider> bindings_;
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> root_;
  fbl::RefPtr<ServiceProviderDirImpl> parent_;
  zx::channel backing_dir_;
  fxl::WeakPtrFactory<ServiceProviderDirImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceProviderDirImpl);
};

}  // namespace sys
}  // namespace fuchsia

#endif  // GARNET_BIN_APPMGR_SERVICE_PROVIDER_DIR_IMPL_H_
