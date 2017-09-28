// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SVC_CPP_SERVICE_NAMESPACE_H_
#define LIB_SVC_CPP_SERVICE_NAMESPACE_H_

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

class ServiceNamespace : public svcfs::ServiceProvider,
                         public app::ServiceProvider {
 public:
  // |ServiceConnector| is the generic, type-unsafe interface for objects used
  // by |ServiceNamespace| to connect generic "interface requests" (i.e.,
  // just channels) specified by service name to service implementations.
  using ServiceConnector = std::function<void(zx::channel)>;

  // A |InterfaceRequestHandler<Interface>| is simply a function that
  // handles an interface request for |Interface|. If it determines that the
  // request should be "accepted", then it should "connect" ("take ownership
  // of") request. Otherwise, it can simply drop |interface_request| (as implied
  // by the interface).
  template <typename Interface>
  using InterfaceRequestHandler =
      std::function<void(fidl::InterfaceRequest<Interface> interface_request)>;

  // Constructs this service namespace implementation in an unbound state.
  ServiceNamespace();

  // Constructs this service provider implementation, binding it to the given
  // interface request. Note: If |request| is not valid ("pending"), then the
  // object will be put into an unbound state.
  explicit ServiceNamespace(
      fidl::InterfaceRequest<app::ServiceProvider> request);

  ~ServiceNamespace() override;

  // Binds this service provider implementation to the given interface request.
  // Multiple bindings may be added.  They are automatically removed when closed
  // remotely.
  void AddBinding(fidl::InterfaceRequest<app::ServiceProvider> request);

  // Disconnect this service provider implementation and put it in a state where
  // it can be rebound to a new request (i.e., restores this object to an
  // unbound state). This may be called even if this object is already unbound.
  void Close();

  // Adds a supported service with the given |service_name|, using the given
  // |service_connector|.
  void AddServiceForName(ServiceConnector connector,
                         const std::string& service_name);

  // Adds a supported service with the given |service_name|, using the given
  // |interface_request_handler| (see above for information about
  // |InterfaceRequestHandler<Interface>|). |interface_request_handler| should
  // remain valid for the lifetime of this object.
  //
  // A typical usage may be:
  //
  //   service_namespace_->AddService<Foobar>(
  //       [](InterfaceRequest<FooBar> foobar_request) {
  //         foobar_binding_.AddBinding(this, std::move(foobar_request));
  //       });
  template <typename Interface>
  void AddService(InterfaceRequestHandler<Interface> handler,
                  const std::string& service_name = Interface::Name_) {
    AddServiceForName(
        [handler](zx::channel channel) {
          handler(fidl::InterfaceRequest<Interface>(std::move(channel)));
        },
        service_name);
  }

  // Removes support for the service with the given |service_name|.
  void RemoveServiceForName(const std::string& service_name);

  // Like |RemoveServiceForName()| (above), but designed so that it can be used
  // like |RemoveService<Interface>()| or even
  // |RemoveService<Interface>(service_name)| (to parallel
  // |AddService<Interface>()|).
  template <typename Interface>
  void RemoveService(const std::string& service_name = Interface::Name_) {
    RemoveServiceForName(service_name);
  }

  // Serves a directory containing these services on the given channel.
  //
  // Returns true on success.
  bool ServeDirectory(zx::channel channel);

  // Retuns a file descriptor to a directory containing these services.
  int OpenAsFileDescriptor();

  // Mounts this service namespace at the given path in the file system.
  bool MountAtPath(const char* path);

 private:
  // Overridden from |svcfs::ServiceProvider|:
  void Connect(fbl::StringPiece name, zx::channel channel) override;

  // Overridden from |app::ServiceProvider|:
  void ConnectToService(const fidl::String& service_name,
                        zx::channel channel) override;

  void ConnectCommon(const std::string& service_name, zx::channel channel);

  std::unordered_map<std::string, ServiceConnector> name_to_service_connector_;

  fs::ManagedVfs vfs_;
  fbl::RefPtr<svcfs::VnodeDir> directory_;
  fidl::BindingSet<app::ServiceProvider> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceNamespace);
};

}  // namespace app

#endif  // LIB_SVC_CPP_SERVICE_NAMESPACE_H_
