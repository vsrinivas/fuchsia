// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_LIB_SVC_SERVICE_PROVIDER_BRIDGE_H_
#define APPLICATION_LIB_SVC_SERVICE_PROVIDER_BRIDGE_H_

#include <fbl/ref_ptr.h>
#include <fs/managed-vfs.h>
#include <svcfs/svcfs.h>
#include <zx/channel.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

#include "lib/app/fidl/service_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"

namespace app {

// ServiceProviderBridge is a bridge between a service provider and a service
// directory.
//
// The bridge takes a service provider to use as a backend and exposes both the
// service provider interface and the directory interface, which will make it
// easier to migrate clients to the directory interface.
class ServiceProviderBridge : public svcfs::ServiceProvider,
                              public app::ServiceProvider {
 public:
  ServiceProviderBridge();
  ~ServiceProviderBridge() override;

  using ServiceConnector = std::function<void(zx::channel)>;

  template <typename Interface>
  using InterfaceRequestHandler =
      std::function<void(fidl::InterfaceRequest<Interface> interface_request)>;

  void AddServiceForName(ServiceConnector connector,
                         const std::string& service_name);

  template <typename Interface>
  void AddService(InterfaceRequestHandler<Interface> handler,
                  const std::string& service_name = Interface::Name_) {
    AddServiceForName(
        [handler](zx::channel channel) {
          handler(fidl::InterfaceRequest<Interface>(std::move(channel)));
        },
        service_name);
  }

  void set_backend(app::ServiceProviderPtr backend) {
    backend_ = std::move(backend);
  }

  void AddBinding(fidl::InterfaceRequest<app::ServiceProvider> request);
  bool ServeDirectory(zx::channel channel);

  zx::channel OpenAsDirectory();
  int OpenAsFileDescriptor();

 private:
  // Overridden from |svcfs::ServiceProvider|:
  void Connect(const char* name, size_t len, zx::channel channel) override;

  // Overridden from |app::ServiceProvider|:
  void ConnectToService(const fidl::String& service_name,
                        zx::channel channel) override;

  fs::ManagedVfs vfs_;
  fidl::BindingSet<app::ServiceProvider> bindings_;
  fbl::RefPtr<svcfs::VnodeProviderDir> directory_;

  std::map<std::string, ServiceConnector> name_to_service_connector_;
  app::ServiceProviderPtr backend_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceProviderBridge);
};

}  // namespace app

#endif  // APPLICATION_LIB_SVC_SERVICE_PROVIDER_BRIDGE_H_
