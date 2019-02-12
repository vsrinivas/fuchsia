// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_COMPONENT2_CPP_OUTGOING_H_
#define LIB_COMPONENT2_CPP_OUTGOING_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>

#include <memory>
#include <utility>

namespace component2 {

// The services and data published by this component.
class Outgoing {
 public:
  Outgoing();
  ~Outgoing();

  Outgoing(const Outgoing&) = delete;
  Outgoing& operator=(const Outgoing&) = delete;

  // TODO: Add debug and ctrl directories.
  // TODO: Add a mechanism for adding more directories.

  // Start serving the root directory on the given channel.
  //
  // If |dispatcher| is NULL, this object will serve the root directory using
  // the |async_dispatcher_t| from |async_get_default_dispatcher()|.
  zx_status_t Serve(zx::channel directory_request,
                    async_dispatcher_t* dispatcher = nullptr);

  // Start serving the root directory on the channel provided to this process at
  // startup as PA_DIRECTORY_REQUEST.
  zx_status_t ServeFromStartupInfo(async_dispatcher_t* dispatcher = nullptr);

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
      const std::string& name = Interface::Name_) const {
    return public_->AddEntry(
        name, std::make_unique<vfs::Service>(std::move(handler)));
  }

  // Removes the specified interface from the set of public interfaces.
  template <typename Interface>
  zx_status_t RemovePublicService(
      const std::string& name = Interface::Name_) const {
    return public_->RemoveEntry(name);
  }

 private:
  std::unique_ptr<vfs::PseudoDir> root_;

  // Owned by root_.
  vfs::PseudoDir* public_;
};

}  // namespace component2

#endif  // LIB_COMPONENT2_CPP_OUTGOING_H_
