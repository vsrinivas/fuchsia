// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_CPP_OUTGOING_H_
#define LIB_APP_CPP_OUTGOING_H_

#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <lib/fit/function.h>

#include <memory>

#include "lib/svc/cpp/service_namespace.h"

namespace fuchsia {
namespace sys {

// The services and data published by this component.
class Outgoing {
 public:
  Outgoing();
  ~Outgoing();

  Outgoing(const Outgoing&) = delete;
  Outgoing& operator=(const Outgoing&) = delete;

  // Gets a service provider implementation by which the application can
  // provide outgoing services back to its creator.
  //
  // Please use AddPublicService or public_dir() instead.
  ServiceNamespace* deprecated_services() const {
    return &deprecated_outgoing_services_;
  }

  // Gets the directory which is the root of the tree of file system objects
  // exported by this application to the rest of the system.
  //
  // Clients should organize exported objects into sub-directories by role
  // using conventions such as the following:
  //
  // - public: services published for clients
  // - debug: debugging information exported by the application
  // - ctrl: services published for appmgr
  const fbl::RefPtr<fs::PseudoDir>& root_dir() const { return root_dir_; }

  // Gets an export sub-directory called "public" for publishing services for
  // clients.
  const fbl::RefPtr<fs::PseudoDir>& public_dir() const { return public_dir_; }

  // Gets an export sub-directory called "debug" for publishing debugging
  // information.
  const fbl::RefPtr<fs::PseudoDir>& debug_dir() const { return debug_dir_; }

  // Gets an export sub-directory called "ctrl" for publishing services for
  // appmgr.
  const fbl::RefPtr<fs::PseudoDir>& ctrl_dir() const { return ctrl_dir_; }

  // Start serving the root directory on the given channel.
  zx_status_t Serve(zx::channel dir_request);

  // Start serving the root directory on the channel provided to this process at
  // startup as PA_DIRECTORY_REQUEST.
  zx_status_t ServeFromStartupInfo();

  // Adds the specified interface to the set of public interfaces.
  //
  // Adds a supported service with the given |service_name|, using the given
  // |interface_request_handler|. |interface_request_handler| should
  // remain valid for the lifetime of this object.
  //
  // A typical usage may be:
  //
  //   AddPublicService(foobar_bindings_.GetHandler(this));
  template <typename Interface>
  zx_status_t AddPublicService(
      fidl::InterfaceRequestHandler<Interface> handler,
      const std::string& service_name = Interface::Name_) const {
    return public_dir()->AddEntry(
        service_name.c_str(),
        fbl::AdoptRef(new fs::Service(
            [handler = std::move(handler)](zx::channel channel) {
              handler(fidl::InterfaceRequest<Interface>(std::move(channel)));
              return ZX_OK;
            })));
  }

  // Removes the specified interface from the set of public interfaces.
  template <typename Interface>
  zx_status_t RemovePublicService(
      const std::string& name = Interface::Name_) const {
    return public_dir()->RemoveEntry(name.c_str());
  }

 private:
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> root_dir_;
  fbl::RefPtr<fs::PseudoDir> public_dir_;
  fbl::RefPtr<fs::PseudoDir> debug_dir_;
  fbl::RefPtr<fs::PseudoDir> ctrl_dir_;

  mutable ServiceNamespace deprecated_outgoing_services_;
};

}  // namespace sys
}  // namespace fuchsia

#endif  // LIB_APP_CPP_OUTGOING_H_
