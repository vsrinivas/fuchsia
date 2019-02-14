// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_CPP_TESTING_SERVICE_DIRECTORY_FOR_TEST_H_
#define LIB_SYS_CPP_TESTING_SERVICE_DIRECTORY_FOR_TEST_H_

#include "lib/sys/cpp/service_directory.h"

#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>

namespace sys {
namespace testing {

// A fake |ServiceDirectory| for unit testing.
// Does not accessing services outside of this object.
class ServiceDirectoryForTest final : public sys::ServiceDirectory {
 public:
  // Create instance of this class.
  static std::shared_ptr<ServiceDirectoryForTest> Create(
      async_dispatcher_t* dispatcher = nullptr);

  // Don't use this method, instead use |Create|.
  ServiceDirectoryForTest(zx::channel directory,
                          std::unique_ptr<vfs::PseudoDir> svc_dir);

  ~ServiceDirectoryForTest() override;

  // Injects a service which can be accessed by calling Connect on
  // |sys::ServiceDirectory| by code under test.
  //
  // Adds a supported service with the given |service_name|, using the given
  // |interface_request_handler|. |interface_request_handler| should
  // remain valid for the lifetime of this object.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: This already contains an entry for
  // this service.
  //
  // # Example
  //
  // ```
  // fidl::BindingSet<fuchsia::foo::Controller> bindings;
  // svc->AddService(bindings.GetHandler(this));
  // ```
  template <typename Interface>
  zx_status_t AddService(fidl::InterfaceRequestHandler<Interface> handler,
                         const std::string& name = Interface::Name_) const {
    return svc_dir_->AddEntry(
        name, std::make_unique<vfs::Service>(std::move(handler)));
  }

 private:
  std::unique_ptr<vfs::PseudoDir> svc_dir_;
};

}  // namespace testing
}  // namespace sys

#endif  // LIB_SYS_CPP_TESTING_SERVICE_DIRECTORY_FOR_TEST_H_
