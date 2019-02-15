// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_CPP_OUTGOING_H_
#define LIB_SYS_CPP_OUTGOING_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>

#include <memory>
#include <utility>

namespace sys {

// The directory provided by this component to the component manager.
//
// A components outgoing directory contains services, data, and other objects
// that can be consumed by either the component manager itself or by other
// components in the system.
//
// The root directory contains serveral directories with well-known names:
//
//  * public. This directory contains the services offered by this component to
//    other components.
//
// TODO: Add debug and ctrl directories.
// TODO: Add a mechanism for adding more directories.
//
// The root directory is typically used to service the |PA_DIRECTORY_REQUEST|
// process argument.
//
// Instances of this class are thread-safe.
class Outgoing final {
 public:
  Outgoing();
  ~Outgoing();

  // Outgoing objects cannot be copied.
  Outgoing(const Outgoing&) = delete;
  Outgoing& operator=(const Outgoing&) = delete;

  // Start serving the root directory on the given channel.
  //
  // This object will implement the |fuchsia.io.Directory| interface using this
  // channel.
  //
  // If |dispatcher| is NULL, this object will serve the root directory using
  // the |async_dispatcher_t| from |async_get_default_dispatcher()|.
  //
  // # Errors
  //
  // ZX_ERR_BAD_HANDLE: |directory_request| is not a valid handle.
  //
  // ZX_ERR_ACCESS_DENIED: |directory_request| has insufficient rights.
  //
  // TODO: Document more errors.
  zx_status_t Serve(zx::channel directory_request,
                    async_dispatcher_t* dispatcher = nullptr);

  // Start serving the root directory on the channel provided to this process at
  // startup as |PA_DIRECTORY_REQUEST|.
  //
  // This object will implement the |fuchsia.io.Directory| interface using this
  // channel.
  //
  // If |dispatcher| is NULL, this object will serve the root directory using
  // the |async_dispatcher_t| from |async_get_default_dispatcher()|.
  //
  // # Errors
  //
  // ZX_ERR_BAD_HANDLE: |directory_request| is not a valid handle.
  //
  // ZX_ERR_ACCESS_DENIED: |directory_request| has insufficient rights.
  //
  // TODO: Document more errors.
  zx_status_t ServeFromStartupInfo(async_dispatcher_t* dispatcher = nullptr);

  // Adds the specified interface to the set of public interfaces.
  //
  // Adds a supported service with the given |service_name|, using the given
  // |interface_request_handler|. |interface_request_handler| should
  // remain valid for the lifetime of this object.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: The public directory already contains an entry for
  // this service.
  //
  // # Example
  //
  // ```
  // fidl::BindingSet<fuchsia::foo::Controller> bindings;
  // outgoing.AddPublicService(bindings.GetHandler(this));
  // ```
  template <typename Interface>
  zx_status_t AddPublicService(fidl::InterfaceRequestHandler<Interface> handler,
                               std::string name = Interface::Name_) const {
    return public_->AddEntry(
        std::move(name), std::make_unique<vfs::Service>(std::move(handler)));
  }

  // Removes the specified interface from the set of public interfaces.
  //
  // # Errors
  //
  // ZX_ERR_NOT_FOUND: The public directory does not contain an entry for this
  // service.
  //
  // # Example
  //
  // ```
  // outgoing.RemovePublicService<fuchsia::foo::Controller>();
  // ```
  template <typename Interface>
  zx_status_t RemovePublicService(
      const std::string& name = Interface::Name_) const {
    return public_->RemoveEntry(name);
  }

  // Get access to debug directory to publish debug data.
  // This directory is owned by this class.
  vfs::PseudoDir* debug_dir() { return debug_; }

  // Get access to ctrl directory to publish ctrl data.
  // This directory is owned by this class.
  vfs::PseudoDir* ctrl_dir() { return ctrl_; }

 private:
  // Adds a new empty directory to |root_| and returns pointer to new directory.
  // Will fail silently if directory with that name already exists.
  vfs::PseudoDir* AddNewEmptyDirectory(std::string name);

  // The root outgoing directory itself.
  std::unique_ptr<vfs::PseudoDir> root_;

  // The public subdirectory of the root directory.
  //
  // The underlying |vfs::PseudoDir| object is owned by |root_|.
  vfs::PseudoDir* public_;

  // The debug subdirectory of the root directory.
  //
  // The underlying |vfs::PseudoDir| object is owned by |root_|.
  vfs::PseudoDir* debug_;

  // The ctrl subdirectory of the root directory.
  //
  // The underlying |vfs::PseudoDir| object is owned by |root_|.
  vfs::PseudoDir* ctrl_;
};

}  // namespace sys

#endif  // LIB_SYS_CPP_OUTGOING_H_
