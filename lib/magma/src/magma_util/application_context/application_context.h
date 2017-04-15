// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FAUX_APPLICATION_CONTEXT_H
#define FAUX_APPLICATION_CONTEXT_H

#include "application/lib/app/service_provider_impl.h"
#include "application/services/service_provider.fidl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"

namespace faux {

// Provides access to the application's environment and allows the application
// to publish outgoing services back to its creator.
class ApplicationContext {
public:
    // The constructor is normally called by CreateFromStartupInfo().
    ApplicationContext(fidl::InterfaceHandle<app::ServiceProvider> environment);

    ~ApplicationContext();

    // Creates the application context from the process startup info.
    //
    // This function should be called once during process initialization to
    // retrieve the handles supplied to the application by the application
    // manager.
    //
    // This function will call FTL_CHECK and stack dump if the environment is
    // null. However, a null environment services pointer is allowed.
    //
    // The returned unique_ptr is never null.
    static std::unique_ptr<ApplicationContext> CreateFromStartupInfo();

    // Like CreateFromStartupInfo(), but allows both the environment and the
    // environment services to be null so that callers can validate the values
    // and provide meaningful error messages.
    static std::unique_ptr<ApplicationContext> CreateFromStartupInfoNotChecked();

    // Gets the application's environment.
    //
    // May be null if the application does not have access to its environment.
    // const ApplicationEnvironmentPtr& environment() const { return environment_; }

    // Gets incoming services provided to the application by the host of
    // its environment.
    //
    // May be null if the application does not have access to its environment.
    const app::ServiceProviderPtr& environment_services() const { return environment_services_; }

    // Gets the application launcher service provided to the application by
    // its environment.
    //
    // May be null if the application does not have access to its environment.
    // const ApplicationLauncherPtr& launcher() const { return launcher_; }

    // Gets a service provider implementation by which the application can
    // provide outgoing services back to its creator.
    // ServiceProviderImpl* outgoing_services() { return &outgoing_services_; }

    // Connects to a service provided by the application's environment,
    // returning an interface pointer.
    template <typename Interface>
    fidl::InterfacePtr<Interface>
    ConnectToEnvironmentService(const std::string& interface_name = Interface::Name_)
    {
        fidl::InterfacePtr<Interface> interface_ptr;
        printf("connecting to service %s\n", interface_name.c_str());
        environment_services_->ConnectToService(interface_name,
                                                interface_ptr.NewRequest().PassChannel());
        return interface_ptr;
    }

    // Connects to a service provided by the application's environment,
    // binding the service to an interface request.
    template <typename Interface>
    void ConnectToEnvironmentService(fidl::InterfaceRequest<Interface> interface_request,
                                     const std::string& interface_name = Interface::Name_)
    {
        environment_services_->ConnectToService(interface_name, interface_request.PassChannel());
    }

private:
    app::ServiceProviderPtr environment_services_;

    FTL_DISALLOW_COPY_AND_ASSIGN(ApplicationContext);
};

} // namespace faux

#endif // APPLICATION_LIB_APP_APPLICATION_CONTEXT_H_
