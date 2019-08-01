// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_SERVICE_CPP_SERVICE_HANDLER_H_
#define LIB_FIDL_SERVICE_CPP_SERVICE_HANDLER_H_

#include <lib/fidl/cpp/interface_request.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>

namespace fidl {

// A handler for an instance of a service.
class ServiceHandler {
 public:
  // Add a member of |name| to the instance, which is handled by |handler|.
  template <typename Protocol>
  zx_status_t AddMember(std::string name, fidl::InterfaceRequestHandler<Protocol> handler) {
    return AddMember(std::move(name), std::make_unique<vfs::Service>(std::move(handler)));
  }

  // Add a member of |name| to the instance, which is handled by |node|.
  zx_status_t AddMember(std::string name, std::unique_ptr<vfs::Service> node) {
    return dir_->AddEntry(std::move(name), std::move(node));
  }

  // Take the underlying pseudo-directory from the service handler.
  //
  // Once taken, the service handler is no longer safe to use.
  std::unique_ptr<vfs::PseudoDir> TakeDirectory() { return std::move(dir_); }

 private:
  std::unique_ptr<vfs::PseudoDir> dir_ = std::make_unique<vfs::PseudoDir>();
};

}  // namespace fidl

#endif  // LIB_FIDL_SERVICE_CPP_SERVICE_HANDLER_H_
