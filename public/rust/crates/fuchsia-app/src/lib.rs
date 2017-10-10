//! Connect to or provide Fuchsia services.

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![deny(missing_docs)]

extern crate fuchsia_zircon as zircon;
extern crate mxruntime;
extern crate fidl;
extern crate futures;
extern crate tokio_core;

// Generated FIDL bindings
extern crate garnet_public_lib_app_fidl_service_provider;
extern crate garnet_public_lib_app_fidl;

use garnet_public_lib_app_fidl::{
    ApplicationController,
    ApplicationEnvironment,
    ApplicationLauncher,
    ApplicationLaunchInfo,
};
use garnet_public_lib_app_fidl_service_provider::ServiceProvider;
use fidl::FidlService;
use zircon::Channel;
use std::io;
use tokio_core::reactor::Handle as TokioHandle;

/// Tools for starting or connecting to existing Fuchsia applications and services.
pub mod client {
    use super::*;

    #[inline]
    /// Connect to a FIDL service on the given `service_provider`.
    pub fn connect_to_service<Service: FidlService>(
        service_provider: &ServiceProvider::Proxy,
        handle: &TokioHandle
    ) -> Result<Service::Proxy, fidl::Error>
    {
        let (proxy, server_end) = Service::new_pair(handle)?;
        service_provider.connect_to_service(String::from(Service::NAME), server_end.into_channel())?;
        Ok(proxy)
    }

    /// ApplicationContext provides access to the environment of the currently-running application.
    pub struct ApplicationContext {
        // TODO: use somehow?
        #[allow(dead_code)]
        app_env: ApplicationEnvironment::Proxy,

        service_provider: ServiceProvider::Proxy,
    }

    impl ApplicationContext {
        /// Initialize the context for the currently-running application.
        pub fn new(handle: &TokioHandle) -> Result<Self, fidl::Error> {
            let service_root = mxruntime::get_service_root()?;
            let app_env_channel =
                mxruntime::connect_to_environment_service(
                    service_root,
                    ApplicationEnvironment::Service::NAME)?;

            let app_env_client = fidl::ClientEnd::new(app_env_channel);
            let app_env = ApplicationEnvironment::new_proxy(app_env_client, handle)?;
            let (service_provider, service_provider_server_end) = ServiceProvider::new_pair(handle)?;
            app_env.get_services(service_provider_server_end)?;

            Ok(ApplicationContext {
                app_env,
                service_provider,
            })
        }

        #[inline]
        /// Connect to a service provided through the current application's environment.
        ///
        /// This connection is made using the application environment's service provider.
        pub fn connect_to_service<Service: FidlService>(&self, handle: &TokioHandle)
            -> Result<Service::Proxy, fidl::Error>
        {
            connect_to_service::<Service>(&self.service_provider, handle)
        }
    }

    /// Launcher launches Fuchsia applications.
    pub struct Launcher {
        app_launcher: ApplicationLauncher::Proxy,
    }

    impl Launcher {
        #[inline]
        /// Create a new application launcher.
        pub fn new(
            context: &ApplicationContext,
            handle: &TokioHandle) -> Result<Self, fidl::Error>
        {
            let app_launcher = context.connect_to_service::<ApplicationLauncher::Service>(handle)?;
            Ok(Launcher { app_launcher })
        }

        /// Launch an application at the specified URL.
        pub fn launch(
            &self,
            url: String,
            arguments: Option<Vec<String>>,
            handle: &TokioHandle
        ) -> Result<App, fidl::Error>
        {
            let (service_provider, service_provider_server_end) = ServiceProvider::Service::new_pair(handle)?;
            let (app_controller, controller_server_end) = ApplicationController::Service::new_pair(handle)?;

            let launch_info = ApplicationLaunchInfo {
                url,
                arguments,
                service_request: None,
                flat_namespace: None,
                services: Some(service_provider_server_end),
                additional_services: None,
            };

            self.app_launcher.create_application(launch_info, Some(controller_server_end))?;
            Ok(App { service_provider, app_controller })
        }
    }

    /// `App` represents a launched application.
    pub struct App {
        service_provider: ServiceProvider::Proxy,

        // TODO: use somehow?
        #[allow(dead_code)]
        app_controller: ApplicationController::Proxy,
    }

    impl App {
        #[inline]
        /// Connect to a service provided by the `App`.
        pub fn connect_to_service<Service: FidlService>(&self, handle: &TokioHandle)
            -> Result<Service::Proxy, fidl::Error>
        {
            connect_to_service::<Service>(&self.service_provider, handle)
        }
    }
}

/// Tools for providing Fuchsia services.
pub mod server {
    use super::*;
    use futures::{future, Future, Poll};

    type ServerInner<Services> =
        fidl::Server<ServiceProvider::Dispatcher<ServiceProviderServer<Services>>>;

    #[must_use = "futures do nothing unless polled"]
    /// `Server` is a future which, when polled, launches Fuchsia services upon request.
    pub struct Server<Services: ServiceFactories>(ServerInner<Services>);

    impl<Services: ServiceFactories> Server<Services> {
        /// Create a new `Server` to serve service connection requests on the specified channel.
        pub fn new(services: ServiceProviderServer<Services>, channel: zircon::Channel, handle: &TokioHandle) -> Result<Self, fidl::Error> {
            Ok(Server(fidl::Server::new(
                ServiceProvider::Dispatcher(services),
                channel,
                handle
            )?))
        }

        /// Create a `Server` which serves serves connection requests on the outgoing service channel.
        pub fn new_outgoing(services: ServiceProviderServer<Services>, handle: &TokioHandle) -> Result<Self, fidl::Error> {
            let channel = zircon::Channel::from(
                mxruntime::get_startup_handle(mxruntime::HandleType::OutgoingServices)
                    .ok_or(io::Error::new(io::ErrorKind::NotFound, "No OutgoingServices handle found"))?);

            Self::new(services, channel, handle)
        }
    }

    impl<Services: ServiceFactories> Future for Server<Services> {
        type Item = <ServerInner<Services> as Future>::Item;
        type Error = <ServerInner<Services> as Future>::Error;

        fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
            self.0.poll()
        }
    }

    /// A heterogeneous list.
    pub struct HCons<Head, Tail> {
        head: Head,
        tail: Tail,
    }

    /// The "empty" tail of a heterogeneous list.
    pub struct HNil;

    /// `ServiceFactory` lazily creates instances of services.
    ///
    /// Note that this trait is implemented by `FnMut` closures like `|| MyService { ... }`.
    pub trait ServiceFactory {

        /// The concrete type of the `fidl::Stub` service created by this `ServiceFactory`.
        type Stub: fidl::Stub + 'static;

        /// Create a `fidl::Stub` service.
        fn create(&mut self) -> Self::Stub;
    }

    impl<F, S> ServiceFactory for F
        where F: FnMut() -> S,
            S: fidl::Stub + 'static
    {
        type Stub = S;

        #[inline]
        fn create(&mut self) -> Self::Stub {
            (self)()
        }
    }

    /// A collection of `ServiceFactory`s.
    pub trait ServiceFactories {
        /// Spawn a service of type `service_name` on `channel`.
        fn spawn_service(&mut self, service_name: String, channel: Channel, handle: &TokioHandle);
    }

    impl ServiceFactories for HNil {
        #[inline]
        fn spawn_service(&mut self, service_name: String, _: Channel, _: &TokioHandle) {
            // TODO: proper logging
            println!("No service found with name \"{}\"", service_name);
        }
    }

    impl<Factory, Tail> ServiceFactories for HCons<Factory, Tail>
        where Factory: ServiceFactory,
            Tail: ServiceFactories
    {
        #[inline]
        fn spawn_service(&mut self, service_name: String, channel: Channel, handle: &TokioHandle) {
            if service_name == <Factory::Stub as fidl::Stub>::Service::NAME {
                match fidl::Server::new(self.head.create(), channel, handle) {
                    Ok(server) => {
                        handle.spawn(server.map_err(|e|
                            // TODO: proper logging
                            println!("Error running server: {:?}", e)
                        ));
                    }
                    Err(e) => {
                        // TODO: proper logging
                        println!("Error starting service \"{}\": {:?}", service_name, e);
                    }
                }
            } else {
                self.tail.spawn_service(service_name, channel, handle);
            }
        }
    }

    /// `ServiceProviderServer` is a server which manufactures service instances of varying types on demand.
    /// To run a `ServiceProviderServer`, use `Server::new` or `Server::new_outgoing`.
    pub struct ServiceProviderServer<Services: ServiceFactories> {
        handle: TokioHandle,
        services: Services,
    }

    impl ServiceProviderServer<HNil> {
        /// Create a new `ServiceProviderServer` which doesn't provide any services.
        pub fn new(handle: &TokioHandle) -> Self {
            ServiceProviderServer {
                handle: handle.clone(),
                services: HNil,
            }
        }
    }

    impl<Services: ServiceFactories> ServiceProviderServer<Services> {
        /// Add a service to the `ServiceProviderServer`.
        pub fn add_service<S: ServiceFactory>(self, service_factory: S) -> ServiceProviderServer<HCons<S, Services>> {
            ServiceProviderServer {
                handle: self.handle,
                services: HCons {
                    head: service_factory,
                    tail: self.services,
                }
            }
        }
    }

    impl<Services: ServiceFactories> ServiceProvider::Server for ServiceProviderServer<Services> {
        type ConnectToService = future::FutureResult<(), fidl::CloseChannel>;
        fn connect_to_service(&mut self, service_name: String, channel: Channel)
            -> Self::ConnectToService
        {
            self.services.spawn_service(service_name, channel, &self.handle);
            future::ok(())
        }
    }
}
