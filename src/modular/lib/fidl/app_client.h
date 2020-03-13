// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_FIDL_APP_CLIENT_H_
#define SRC_MODULAR_LIB_FIDL_APP_CLIENT_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/svc/cpp/services.h>

#include <memory>
#include <string>

#include "src/lib/fxl/macros.h"
#include "src/modular/lib/common/async_holder.h"

namespace modular {

// A class that holds a connection to a single service instance in an
// application instance. The service instance supports life cycle with a
// Terminate() method. When calling Terminate(), the service is supposed to
// close its connection, and when that happens, we can kill the application, or
// it's gone already anyway. If the service connection doesn't close after a
// timeout, we close it and kill the application anyway.
//
// When starting an application instance, the directory pointed to by
// |data_origin| will be mapped into /data for the newly started application.
// If left empty, it'll be mapped to the root /data.
//
// |additional_services| will allow us to add custom services to an applications
// namespace.
//
// |flat_namespace| allows us to add custom directories to an application's
// namespace.
//
// AppClientBase are the non-template parts factored out so they don't need to
// be inline. It can be used on its own too.
class AppClientBase : public AsyncHolderBase {
 public:
  AppClientBase(fuchsia::sys::Launcher* launcher, fuchsia::modular::AppConfig config,
                std::string data_origin = "",
                fuchsia::sys::ServiceListPtr additional_services = nullptr,
                fuchsia::sys::FlatNamespacePtr flat_namespace = nullptr);

  ~AppClientBase() override;

  // Gives access to the services of the started application. Services
  // obtained from it are not involved in life cycle management provided by
  // AppClient, however. This is used for example to obtain the ViewProvider.
  component::Services& services() { return services_; }

  // Registers a handler to receive a notification when this application
  // connection encounters an error. This typically happens when this
  // application stops or crashes. |error_handler| will be deregistered when
  // attempting graceful termination via |AsyncHolderBase::Teardown()|.
  void SetAppErrorHandler(fit::function<void()> error_handler);

 private:
  // The termination sequence as prescribed by AsyncHolderBase.
  void ImplTeardown(fit::function<void()> done) override;
  void ImplReset() override;

  // Service specific parts of the termination sequence.
  virtual void ServiceTerminate(fit::function<void()> done);
  virtual void ServiceUnbind();

  fuchsia::sys::ComponentControllerPtr app_;
  component::Services services_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AppClientBase);
};

// A generic client that does the standard termination sequence. For a service
// with another termination sequence, another implementation could be created.
template <class Service>
class AppClient : public AppClientBase {
 public:
  AppClient(fuchsia::sys::Launcher* const launcher, fuchsia::modular::AppConfig config,
            std::string data_origin = "",
            fuchsia::sys::ServiceListPtr additional_services = nullptr,
            fuchsia::sys::FlatNamespacePtr flat_namespace = nullptr)
      : AppClientBase(launcher, std::move(config), std::move(data_origin),
                      std::move(additional_services), std::move(flat_namespace)) {
    services().ConnectToService(service_.NewRequest());
  }
  ~AppClient() override = default;

  fidl::InterfacePtr<Service>& primary_service() { return service_; }

 private:
  void ServiceTerminate(fit::function<void()> done) override {
    // The service is expected to acknowledge the Terminate() request by
    // closing its connection within the timeout set in Teardown().
    service_.set_error_handler([done = std::move(done)](zx_status_t status) { done(); });
    service_->Terminate();
  }

  void ServiceUnbind() override { service_.Unbind(); }

  fidl::InterfacePtr<Service> service_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AppClient);
};

template <>
void AppClient<fuchsia::modular::Lifecycle>::ServiceTerminate(fit::function<void()> done);

}  // namespace modular

#endif  // SRC_MODULAR_LIB_FIDL_APP_CLIENT_H_
