// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_FIDL_APP_CLIENT_H_
#define APPS_MODULAR_LIB_FIDL_APP_CLIENT_H_

#include <memory>
#include <string>

#include "lib/app/cpp/connect.h"
#include "lib/app/fidl/application_launcher.fidl.h"
#include "apps/modular/services/config/config.fidl.h"
#include "apps/modular/services/lifecycle/lifecycle.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fsl/tasks/message_loop.h"

namespace modular {

constexpr int kAppClientTimeoutSeconds = 1;

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
// AppClientBase are the non-template parts factored out so they don't need to
// be inline. It can be used on its own too.
class AppClientBase {
 public:
  AppClientBase(app::ApplicationLauncher* launcher,
                AppConfigPtr config,
                std::string data_origin = "");
  virtual ~AppClientBase();

  // Gives access to the service provider of the started application. Services
  // obtained from it are not involved in life cycle management provided by
  // AppClient, however. This is used for example to obtain the ViewProvider.
  app::ServiceProvider* services() { return services_.get(); }

  // Invokes the termination sequence for the service. The done callback is
  // invoked after the application controller connection is released either
  // after the service was disconnected or a timeout was reached.
  //
  // The method is called AppTerminate() to distinguish it from the Terminate()
  // method of the service. It's important that the Terminate() method is never
  // invoked through primary_service(), below. TODO(mesch): It would be better
  // if it were impossible outright. This could be accomplished with a separate
  // Terminate interface, which would only be exposed to AppClient.
  void AppTerminate(const std::function<void()>& done,
                    fxl::TimeDelta timeout =
                        fxl::TimeDelta::FromSeconds(kAppClientTimeoutSeconds));

  // Registers a handler to receive a notification when this application
  // connection encounters an error. This typically happens when this
  // application stops or crashes. |error_handler| will be deregistered when
  // attempting graceful termination via |AppTerminate|.
  void SetAppErrorHandler(const std::function<void()>& error_handler);

 private:
  // Service specific parts of the termination sequence.
  virtual void ServiceTerminate(const std::function<void()>& done);
  virtual void ServiceReset();

  const std::string url_;  // For logging only.
  app::ApplicationControllerPtr app_;
  app::ServiceProviderPtr services_;
  FXL_DISALLOW_COPY_AND_ASSIGN(AppClientBase);
};

// A generic client that does that the standard termination sequence. For a
// service with another termination sequence, another implementation could be
// created.
template <class Service>
class AppClient : public AppClientBase {
 public:
  AppClient(app::ApplicationLauncher* const launcher,
            AppConfigPtr config,
            std::string data_origin = "")
      : AppClientBase(launcher, std::move(config), std::move(data_origin)) {
    ConnectToService(services(), service_.NewRequest());
  }

  ~AppClient() override = default;

  fidl::InterfacePtr<Service>& primary_service() { return service_; }

 private:
  void ServiceTerminate(const std::function<void()>& done) override {
    // The service is expected to acknowledge the Terminate() request by
    // closing its connection within the timeout set in AppTerminate().
    service_.set_connection_error_handler(done);
    service_->Terminate();
  }

  void ServiceReset() override { service_.reset(); }

  fidl::InterfacePtr<Service> service_;
  FXL_DISALLOW_COPY_AND_ASSIGN(AppClient);
};

template <>
void AppClient<Lifecycle>::ServiceTerminate(const std::function<void()>& done);

}  // namespace modular

#endif  // APPS_MODULAR_LIB_FIDL_APP_CLIENT_H_
