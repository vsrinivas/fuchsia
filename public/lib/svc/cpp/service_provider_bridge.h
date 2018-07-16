// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SVC_CPP_SERVICE_PROVIDER_BRIDGE_H_
#define LIB_SVC_CPP_SERVICE_PROVIDER_BRIDGE_H_

#include <fbl/ref_ptr.h>
#include <fs/synchronous-vfs.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>

#include <string>
#include <unordered_map>
#include <utility>

#include <fuchsia/sys/cpp/fidl.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace component {

// ServiceProviderBridge is a bridge between a service provider and a service
// directory.
//
// The bridge takes a service provider to use as a backend and exposes both the
// service provider interface and the directory interface, which will make it
// easier to migrate clients to the directory interface.
class ServiceProviderBridge : public fuchsia::sys::ServiceProvider {
 public:
  ServiceProviderBridge();
  ~ServiceProviderBridge() override;

  using ServiceConnector = fit::function<void(zx::channel)>;

  void AddServiceForName(ServiceConnector connector,
                         const std::string& service_name);

  template <typename Interface>
  void AddService(fidl::InterfaceRequestHandler<Interface> handler,
                  const std::string& service_name = Interface::Name_) {
    AddServiceForName(
        [handler = std::move(handler)](zx::channel channel) {
          handler(fidl::InterfaceRequest<Interface>(std::move(channel)));
        },
        service_name);
  }

  void set_backend(fuchsia::sys::ServiceProviderPtr backend) {
    backend_ = std::move(backend);
  }

  void set_backing_dir(zx::channel backing_dir) {
    backing_dir_ = std::move(backing_dir);
  }

  void AddBinding(fidl::InterfaceRequest<ServiceProvider> request);
  bool ServeDirectory(zx::channel channel);

  zx::channel OpenAsDirectory();
  int OpenAsFileDescriptor();

 private:
  // A directory-like object which dynamically creates Service vnodes
  // for any file lookup.  Does not support enumeration since the actual
  // set of services available is not known by the bridge.
  class ServiceProviderDir : public fs::Vnode {
   public:
    explicit ServiceProviderDir(fxl::WeakPtr<ServiceProviderBridge> bridge);
    ~ServiceProviderDir() final;

    // Overridden from |fs::Vnode|:
    zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out,
                       fbl::StringPiece name) final;
    zx_status_t Getattr(vnattr_t* a) final;

   private:
    fxl::WeakPtr<ServiceProviderBridge> const bridge_;
  };

  // Overridden from |ServiceProvider|:
  void ConnectToService(fidl::StringPtr service_name,
                        zx::channel channel) override;

  fs::SynchronousVfs vfs_;
  fidl::BindingSet<ServiceProvider> bindings_;
  fbl::RefPtr<ServiceProviderDir> directory_;

  std::map<std::string, ServiceConnector> name_to_service_connector_;
  fuchsia::sys::ServiceProviderPtr backend_;
  zx::channel backing_dir_;

  fxl::WeakPtrFactory<ServiceProviderBridge> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceProviderBridge);
};

}  // namespace component

#endif  // LIB_SVC_CPP_SERVICE_PROVIDER_BRIDGE_H_
