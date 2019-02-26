// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_CPP_TESTING_STARTUP_CONTEXT_FOR_TEST_H_
#define LIB_SYS_CPP_TESTING_STARTUP_CONTEXT_FOR_TEST_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/startup_context.h>

#include "lib/sys/cpp/testing/service_directory_for_test.h"

namespace sys {
namespace testing {

// A fake |StartupContext| for unit testing.
// Does not allow publishing or accessing services outside of this object.
class StartupContextForTest final : public sys::StartupContext {
 public:
  StartupContextForTest(std::shared_ptr<ServiceDirectoryForTest> svc,
                        fuchsia::io::DirectoryPtr directory_ptr,
                        async_dispatcher_t* dispatcher = nullptr);

  ~StartupContextForTest() override;

  static std::unique_ptr<StartupContextForTest> Create(
      async_dispatcher_t* dispatcher = nullptr);

  // Points to outgoing root directory of outgoing directory, test can get it
  // and try to connect to internal directories/objects/files/services to test
  // code which published them.
  fuchsia::io::DirectoryPtr& outgoing_directory_ptr() {
    return outgoing_directory_ptr_;
  }

  // Connect to public service which was published in "public" directory by
  // code under test.
  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToPublicService(
      const std::string& name = Interface::Name_,
      async_dispatcher_t* dispatcher = nullptr) const {
    fidl::InterfacePtr<Interface> ptr;
    ConnectToPublicService(ptr.NewRequest(dispatcher), name);
    return ptr;
  }

  // Connect to public service which was published in "public" directory by
  // code under test.
  template <typename Interface>
  void ConnectToPublicService(fidl::InterfaceRequest<Interface> request,
                              const std::string& name = Interface::Name_) const {
    fdio_service_connect_at(public_directory_ptr_.channel().get(), name.c_str(),
                            request.TakeChannel().release());
  }

  // This can be used to get fake service directory and inject services
  // which can be accessed by code under test.
  //
  // # Example
  //
  // ```
  // fidl::BindingSet<fuchsia::foo::Controller> bindings;
  // context()->service_directory_for_test()->AddService(bindings.GetHandler(this));
  // ```
  const std::shared_ptr<ServiceDirectoryForTest>& service_directory_for_test()
      const {
    return fake_svc_;
  };

  // Defines the testing surface to be used in conjunction with
  // |StartupContextForTest|.
  class Controller {
   public:
    // Adds the specified interface to the set of incoming services in mocked
    // context.
    //
    // Adds a supported service with the given |service_name|, using the given
    // |interface_request_handler|.
    //
    // A typical usage may be:
    //
    //   AddService(foobar_bindings_.GetHandler(this));
    //
    template <typename Interface>
    zx_status_t AddService(
        fidl::InterfaceRequestHandler<Interface> handler,
        const std::string& service_name = Interface::Name_) const {
      return context_->service_directory_for_test()->AddService(
          std::move(handler), service_name);
    }

    // The |StartupContextForTest| associated with this controller.
    const StartupContextForTest& context() const { return *context_; }

   protected:
    Controller(StartupContextForTest* context) : context_(context){};

   private:
    friend class StartupContextForTest;
    StartupContextForTest* context_;
  };

  // Returns |Controller| for tests.
  // Tests should move the |StartupContextForTest| into the code under test, and
  // use the |Controller| to perform and verify interactions.
  Controller& controller() { return controller_; }

 private:
  Controller controller_;

  fuchsia::io::DirectoryPtr outgoing_directory_ptr_;
  fuchsia::io::DirectoryPtr public_directory_ptr_;
  std::shared_ptr<ServiceDirectoryForTest> fake_svc_;
};

}  // namespace testing
}  // namespace sys

#endif  // LIB_SYS_CPP_TESTING_STARTUP_CONTEXT_FOR_TEST_H_
