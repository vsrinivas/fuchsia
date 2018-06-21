// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SVC_CPP_SERVICE_NAMESPACE_H_
#define LIB_SVC_CPP_SERVICE_NAMESPACE_H_

#include <fbl/ref_ptr.h>
#include <fs/pseudo-dir.h>
#include <fs/synchronous-vfs.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>

#include <string>
#include <unordered_map>
#include <utility>

#include <fuchsia/sys/cpp/fidl.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"

namespace fuchsia {
namespace sys {

// ServiceNamespace lets a client to publish services in the form of a
// directory and provides compatibility with ServiceProvider.
//
// This class will be deprecated and removed once ServiceProvider is replaced
// by direct use of directories for publishing and discoverying services.
class ServiceNamespace : public ServiceProvider {
 public:
  // |ServiceConnector| is the generic, type-unsafe interface for objects used
  // by |ServiceNamespace| to connect generic "interface requests" (i.e.,
  // just channels) specified by service name to service implementations.
  using ServiceConnector = fit::function<void(zx::channel)>;

  // Constructs this service namespace implementation in an unbound state.
  ServiceNamespace();

  // Constructs this service provider implementation, binding it to the given
  // interface request. Note: If |request| is not valid ("pending"), then the
  // object will be put into an unbound state.
  explicit ServiceNamespace(fidl::InterfaceRequest<ServiceProvider> request);

  explicit ServiceNamespace(fbl::RefPtr<fs::PseudoDir> directory);

  ~ServiceNamespace() override;

  // Gets the underlying pseudo-directory.
  const fbl::RefPtr<fs::PseudoDir>& directory() const { return directory_; }

  // Binds this service provider implementation to the given interface request.
  // Multiple bindings may be added.  They are automatically removed when closed
  // remotely.
  void AddBinding(fidl::InterfaceRequest<ServiceProvider> request);

  // Disconnect this service provider implementation and put it in a state where
  // it can be rebound to a new request (i.e., restores this object to an
  // unbound state). This may be called even if this object is already unbound.
  void Close();

  // Adds a supported service with the given |service_name|, using the given
  // |service_connector|.
  void AddServiceForName(ServiceConnector connector,
                         const std::string& service_name);

  // Adds a supported service with the given |service_name|, using the given
  // |InterfaceRequestHandler|, which should remain valid for the lifetime of
  // this object.
  //
  // A typical usage may be:
  //
  //   service_namespace_->AddService(foobar_bindings_.GetHandler(this));
  template <typename Interface>
  void AddService(fidl::InterfaceRequestHandler<Interface> handler,
                  const std::string& service_name = Interface::Name_) {
    AddServiceForName(
        [handler = std::move(handler)](zx::channel channel) {
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

 private:
  // Overridden from |ServiceProvider|:
  void ConnectToService(fidl::StringPtr service_name,
                        zx::channel channel) override;

  void Connect(fbl::StringPiece name, zx::channel channel);
  void ConnectCommon(const std::string& service_name, zx::channel channel);

  std::unordered_map<std::string, ServiceConnector> name_to_service_connector_;

  fbl::RefPtr<fs::PseudoDir> directory_;
  fidl::BindingSet<ServiceProvider> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceNamespace);
};

}  // namespace sys
}  // namespace fuchsia

#endif  // LIB_SVC_CPP_SERVICE_NAMESPACE_H_
