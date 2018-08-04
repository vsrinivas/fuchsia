// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SVC_CPP_SERVICES_H_
#define LIB_SVC_CPP_SERVICES_H_

#include <lib/zx/channel.h>

#include <string>

#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/macros.h"

namespace component {

// Connects to a service located at a path within the directory and binds it to
// an untyped interface request.
// TODO(ZX-1358): Replace use of bare directory channel with suitable interface
// once RIO is ported to FIDL.
void ConnectToService(const zx::channel& directory, zx::channel request,
                      const std::string& service_path);

// Connects to a service located at a path within the directory and binds it to
// a fully-typed interface request.
// By default, uses the interface name as the service's path.
// TODO(ZX-1358): Replace use of bare directory channel with suitable interface
// once RIO is ported to FIDL.
template <typename Interface>
inline void ConnectToService(
    const zx::channel& directory, fidl::InterfaceRequest<Interface> request,
    const std::string& service_path = Interface::Name_) {
  ConnectToService(directory, request.TakeChannel(), service_path);
}

// Connects to a service located at a path within the directory and returns a
// fully-typed interface pointer.
// By default, uses the interface name as the service's path.
// TODO(ZX-1358): Replace use of bare directory channel with suitable interface
// once RIO is ported to FIDL.
template <typename Interface>
inline fidl::InterfacePtr<Interface> ConnectToService(
    const zx::channel& directory,
    const std::string& service_path = Interface::Name_) {
  fidl::InterfacePtr<Interface> client;
  ConnectToService(directory, client.NewRequest(), service_path);
  return client;
}

// Services is a convenience frontend to a directory that contains services.
//
// Services holds an zx::channel that references the directory. Rather than
// calling fdio_service_connect_at, you can call |ConnectToService|, which
// satisfies a fidl::InterfaceRequest using the directory.
//
// Note that the directory may contain files and other objects in addition
// to services.
class Services {
 public:
  Services();
  ~Services();

  Services(Services&& other);

  Services& operator=(Services&& other);

  // Creates a request for a directory and stores the other end of the channel
  // in this object for later use by |Connect|.
  //
  // The returned channel is suitable for use in PA_DIRECTORY_REQUEST.
  zx::channel NewRequest();

  void Bind(zx::channel directory);

  // Connects to a service located at a path within the directory and binds it
  // to an untyped interface request.
  // By default, uses the interface name as the service's path.
  void ConnectToService(zx::channel request,
                        const std::string& service_path) const {
    component::ConnectToService(directory_, std::move(request), service_path);
  }

  // Connects to a service located at a path within the directory and binds it
  // to a fully-typed interface request.
  // By default, uses the interface name as the service's path.
  template <typename Interface>
  void ConnectToService(
      fidl::InterfaceRequest<Interface> request,
      const std::string& service_path = Interface::Name_) const {
    component::ConnectToService<Interface>(directory_, std::move(request),
                                           service_path);
  }

  // Connects to a service located at a path within the directory and returns a
  // fully-typed interface pointer.
  // By default, uses the interface name as the service's path.
  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToService(
      const std::string& service_path = Interface::Name_) const {
    return component::ConnectToService<Interface>(directory_, service_path);
  }

  const zx::channel& directory() const { return directory_; }

 private:
  zx::channel directory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Services);
};

}  // namespace component

#endif  // LIB_SVC_CPP_SERVICES_H_
