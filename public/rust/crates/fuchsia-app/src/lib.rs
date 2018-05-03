// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Connect to or provide Fuchsia services.

#![feature(futures_api, pin, arbitrary_self_types)]

#![deny(warnings)]
#![deny(missing_docs)]

extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate mxruntime;
extern crate fdio;
#[macro_use] extern crate failure;
extern crate fidl;
extern crate fidl_fuchsia_io;
extern crate fidl_fuchsia_sys;
extern crate futures;

use fidl_fuchsia_io::{DirectoryRequestStream, DirectoryRequest, DirectoryObject, NodeAttributes, ObjectInfo};
use fidl_fuchsia_sys::{
    ComponentControllerProxy,
    LauncherMarker,
    LauncherProxy,
    LaunchInfo,
};
#[allow(unused_imports)]
use fidl::endpoints2::{RequestStream, ServiceMarker, Proxy};

#[allow(unused_imports)]
use failure::{Error, ResultExt, Fail};
use futures::prelude::*;
use futures::stream::StreamFuture;

/// Tools for starting or connecting to existing Fuchsia applications and services.
pub mod client {
    use super::*;

    #[inline]
    /// Connect to a FIDL service using the application root namespace.
    pub fn connect_to_service<S: ServiceMarker>()
        -> Result<S::Proxy, Error>
    {
        let (proxy, server) = zx::Channel::create()?;

        let service_path = format!("/svc/{}", S::NAME);
        fdio::service_connect(&service_path, server)
            .with_context(|_| format!("Error connecting to service path: {}", service_path))?;

        let proxy = async::Channel::from_channel(proxy)?;
        Ok(S::Proxy::from_channel(proxy))
    }

    /// Launcher launches Fuchsia applications.
    pub struct Launcher {
        launcher: LauncherProxy,
    }

    impl Launcher {
        #[inline]
        /// Create a new application launcher.
        pub fn new() -> Result<Self, Error> {
            let launcher = connect_to_service::<LauncherMarker>()?;
            Ok(Launcher { launcher })
        }

        /// Launch an application at the specified URL.
        pub fn launch(
            &self,
            url: String,
            arguments: Option<Vec<String>>,
        ) -> Result<App, Error>
        {

            let (controller, controller_server_end) = zx::Channel::create()?;
            let (directory_request, directory_server_chan) = zx::Channel::create()?;

            let mut launch_info = LaunchInfo {
                url,
                arguments,
                out: None,
                err: None,
                directory_request: Some(directory_server_chan),
                flat_namespace: None,
                additional_services: None,
            };


            self.launcher
                .create_component(&mut launch_info, Some(controller_server_end.into()))
                .context("Failed to start a new Fuchsia application.")?;

            let controller = async::Channel::from_channel(controller)?;
            let controller = ComponentControllerProxy::new(controller);

            Ok(App { directory_request, controller })
        }
    }

    /// `App` represents a launched application.
    pub struct App {
        // directory_request is a directory protocol channel
        directory_request: zx::Channel,

        // TODO: use somehow?
        #[allow(dead_code)]
        controller: ComponentControllerProxy,
    }

    impl App {
        #[inline]
        /// Connect to a service provided by the `App`.
        pub fn connect_to_service<S: ServiceMarker>(&self, service: S)
            -> Result<S::Proxy, Error>
        {
            let (client_channel, server_channel) = zx::Channel::create()?;
            self.pass_to_service(service, server_channel)?;
            Ok(S::Proxy::from_channel(async::Channel::from_channel(client_channel)?))
        }

        /// Connect to a service by passing a channel for the server.
        pub fn pass_to_service<S: ServiceMarker>(&self, _: S, server_channel: zx::Channel)
            -> Result<(), Error>
        {
            fdio::service_connect_at(&self.directory_request, S::NAME, server_channel)?;
            Ok(())
        }
    }
}

/// Tools for providing Fuchsia services.
pub mod server {
    use super::*;
    use std::marker::Unpin;
    use std::mem::PinMut;
    use futures::{Future, Poll};
    use futures::{StreamExt};
    use futures::stream::FuturesUnordered;
    use self::errors::*;

    /// New root-level errors that may occur when using the `fuchsia_component::server` module.
    /// Note that these are not the only kinds of errors that may occur: errors the module
    /// may also be caused by `fidl::Error` or `zircon::Status`.
    pub mod errors {
        /// The startup handle on which the FIDL server attempted to start was missing.
        #[derive(Debug, Fail)]
        #[fail(display = "The startup handle on which the FIDL server attempted to start was missing.")]
        pub struct MissingStartupHandle;
    }

    /// `ServiceFactory` lazily creates instances of services.
    ///
    /// Note that this trait is implemented by `FnMut` closures like `|| MyService { ... }`.
    pub trait ServiceFactory: Send + 'static {
        /// The path name of a service.
        ///
        /// Used by the `FdioServer` to know which service to connect incoming requests to.
        fn service_name(&self) -> &str;

        /// Create a `fidl::Stub` service.
        // TODO(cramertj): allow `spawn` calls to fail.
        fn spawn_service(&mut self, channel: async::Channel);
    }

    impl<F> ServiceFactory for (&'static str, F)
        where F: FnMut(async::Channel) + Send + 'static,
    {
        fn service_name(&self) -> &str {
            self.0
        }

        fn spawn_service(&mut self, channel: async::Channel) {
            (self.1)(channel)
        }
    }

    /// `ServicesServer` is a server which manufactures service instances of
    /// varying types on demand.
    /// To run a `ServicesServer`, use `Server::new`.
    pub struct ServicesServer {
        services: Vec<Box<ServiceFactory>>,
    }

    impl ServicesServer {
        /// Create a new `ServicesServer` which doesn't provide any services.
        pub fn new() -> Self {
            ServicesServer { services: vec![] }
        }

        /// Add a service to the `ServicesServer`.
        pub fn add_service<S: ServiceFactory>(mut self, service_factory: S) -> Self {
            self.services.push(Box::new(service_factory));
            self
        }

        /// Start serving directory protocol service requests on the process
        /// PA_DIRECTORY_REQUEST handle
        pub fn start(self) -> Result<FdioServer, Error> {
            let fdio_handle =
                mxruntime::get_startup_handle(mxruntime::HandleType::DirectoryRequest)
                .ok_or(MissingStartupHandle)?;

            let fdio_channel = async::Channel::from_channel(fdio_handle.into())?;

            let mut server = FdioServer {
                connections: FuturesUnordered::new(),
                factories: self.services,
            };

            server.serve_connection(fdio_channel);

            Ok(server)
        }
    }

    /// `FdioServer` is a very basic vfs directory server that only responds to
    /// OPEN and CLONE messages. OPEN always connects the client channel to a
    /// newly spawned fidl service produced by the factory F.
    #[must_use = "futures must be polled"]
    pub struct FdioServer {
        // The open connections to this FdioServer. These connections
        // represent external clients who may attempt to open services.
        connections: FuturesUnordered<StreamFuture<DirectoryRequestStream>>,
        // The collection of services, exported from the current process,
        // which may be opened from external clients.
        factories: Vec<Box<ServiceFactory>>,
    }

    impl Unpin for FdioServer {}

    impl FdioServer {
        fn serve_connection(&mut self, chan: async::Channel) {
            self.connections.push(DirectoryRequestStream::from_channel(chan).into_future())
        }

        fn handle_request(&mut self, req: DirectoryRequest) -> Result<(), Error> {
            match req {
                DirectoryRequest::Clone { flags: _, object, control_handle: _ } => {
                    let service_channel = async::Channel::from_channel(object.into_channel())?;
                    self.serve_connection(service_channel);
                    Ok(())
                }
                DirectoryRequest::Close { responder, } => {
                    responder.send(zx::sys::ZX_OK).map_err(|e| e.into())
                }
                DirectoryRequest::Open { flags: _, mode: _, path, object, control_handle: _, } => {
                    let service_channel = async::Channel::from_channel(object.into_channel())?;

                    // This mechanism to open "public" redirects the service
                    // request to the FDIO Server itself for historical reasons.
                    //
                    // This has the unfortunate implication that "public" can be
                    // continually opened from the directory, resulting in paths
                    // like "public/public/public".
                    //
                    // TODO(smklein): Implement a more realistic pseudo-directory,
                    // capable of distinguishing between different characteristics
                    // of imported / exported directories.
                    if path == "public" {
                        self.serve_connection(service_channel);
                        return Ok(());
                    }

                    match self.factories.iter_mut().find(|factory| factory.service_name() == path) {
                        Some(factory) => factory.spawn_service(service_channel),
                        None => eprintln!("No service found for path {}", path),
                    }
                    Ok(())
                }
                DirectoryRequest::Describe { responder } => {
                    let mut info = ObjectInfo::Directory(DirectoryObject { reserved: 0 } );
                    responder.send(&mut info).map_err(|e| e.into())
                }
                // Unsupported / Ignored methods.
                DirectoryRequest::GetAttr { responder, } => {
                    let mut attrs = NodeAttributes {
                        mode: 0,
                        id: 0,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 0,
                        creation_time: 0,
                        modification_time: 0,
                    };
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED, &mut attrs).map_err(|e| e.into())
                }
                DirectoryRequest::SetAttr { flags: _, attributes:_, responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED).map_err(|e| e.into())
                }
                DirectoryRequest::Ioctl { opcode: _, max_out: _, handles: _, in_: _, responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED,
                                   &mut std::iter::empty(),
                                   &mut std::iter::empty()).map_err(|e| e.into())
                }
                DirectoryRequest::Sync { responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED).map_err(|e| e.into())
                }
                DirectoryRequest::Bind { interface_name: _, control_handle: _ } => {
                    Ok(())
                }
                DirectoryRequest::Unlink { path: _, responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED).map_err(|e| e.into())
                }
                DirectoryRequest::ReadDirents { max_out: _, responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED,
                                   &mut std::iter::empty()).map_err(|e| e.into())
                }
                DirectoryRequest::Rewind { responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED).map_err(|e| e.into())
                }
                DirectoryRequest::GetToken { responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED, None).map_err(|e| e.into())
                }
                DirectoryRequest::Rename { src: _, dst_parent_token: _, dst: _, responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED).map_err(|e| e.into())
                }
                DirectoryRequest::Link { src: _, dst_parent_token: _, dst: _, responder, } => {
                    responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED).map_err(|e| e.into())
                }
            }
        }
    }

    impl Future for FdioServer {
        type Output = Result<(), Error>;

        fn poll(mut self: PinMut<Self>, cx: &mut task::Context) -> Poll<Self::Output> {
            loop {
                match self.connections.poll_next_unpin(cx) {
                    Poll::Ready(Some((maybe_request, stream))) => {
                        if let Some(Ok(request)) = maybe_request {
                            match self.handle_request(request) {
                                Ok(()) => self.connections.push(stream.into_future()),
                                _ => (),
                            }
                        }
                    },
                    Poll::Ready(None) | Poll::Pending => return Poll::Pending,
                }
            }
        }
    }
}
