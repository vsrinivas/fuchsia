//! Connect to or provide Fuchsia services.

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(futures_api, pin, arbitrary_self_types)]
#![deny(warnings)]
#![deny(missing_docs)]

extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate mxruntime;
extern crate fdio;
#[macro_use] extern crate failure;
extern crate fidl;
extern crate futures;

// Generated FIDL bindings
extern crate fidl_fuchsia_sys;

use fidl_fuchsia_sys::{
    ComponentControllerProxy,
    LauncherMarker,
    LauncherProxy,
    LaunchInfo,
};
#[allow(unused_imports)]
use fidl::endpoints2::{ServiceMarker, Proxy};

#[allow(unused_imports)]
use failure::{Error, ResultExt, Fail};
use futures::prelude::*;
use futures::stream::FuturesUnordered;

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

    /// `ServicesServer` is a server which manufactures service instances of varying types on demand.
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

        /// Start serving directory protocol service requests on the process PA_DIRECTORY_REQUEST handle
        pub fn start(self) -> Result<FdioServer, Error> {
            let fdio_handle = mxruntime::get_startup_handle(mxruntime::HandleType::DirectoryRequest)
                .ok_or(MissingStartupHandle)?;

            let fdio_channel = async::Channel::from_channel(fdio_handle.into())?;

            let mut server = FdioServer{
                readers: FuturesUnordered::new(),
                factories: self.services,
            };

            server.serve_channel(fdio_channel);

            Ok(server)
        }
    }

    /// `FdioServer` is a very basic vfs directory server that only responds to
    /// OPEN and CLONE messages. OPEN always connects the client channel to a
    /// newly spawned fidl service produced by the factory F.
    #[must_use = "futures must be polled"]
    pub struct FdioServer {
        readers: FuturesUnordered<async::RecvMsg<zx::MessageBuf>>,
        factories: Vec<Box<ServiceFactory>>,
    }

    impl Unpin for FdioServer {}

    impl FdioServer {
        fn dispatch(&mut self, chan: &async::Channel, buf: zx::MessageBuf) -> zx::MessageBuf {
            // TODO(raggi): provide an alternative to the into() here so that we
            // don't need to pass the buf in owned back and forward.
            let mut msg: fdio::rio::Message = buf.into();

            // open & clone use a different reply channel
            //
            // Note: msg.validate() ensures that open must have exactly one
            // handle, but the message may yet be invalid.
            let reply_channel = match msg.op() {
                fdio::fdio_sys::ZXRIO_OPEN |
                fdio::fdio_sys::ZXRIO_CLONE => {
                    msg.take_handle(0).map(zx::Channel::from)
                }
                _ => None,
            };

            let validation = msg.validate();
            if validation.is_err() ||
                (
                    msg.op() != fdio::fdio_sys::ZXRIO_OPEN &&
                    msg.op() != fdio::fdio_sys::ZXRIO_CLONE
                ) ||
                msg.is_describe() ||
                !reply_channel.is_some()
            {
                eprintln!(
                    "service request channel received invalid/unsupported zxrio request: {:?}",
                    &msg
                );

                if msg.is_describe() {
                    let reply_channel = reply_channel.as_ref().unwrap_or(chan.as_ref());
                    let reply_err = validation.err().unwrap_or(zx::Status::NOT_SUPPORTED);
                    fdio::rio::write_object(reply_channel, reply_err, 0, &[], &mut vec![])
                        .unwrap_or_else(|e| {
                            eprintln!("service request reply write failed with {:?}", e)
                        });
                }

                return msg.into();
            }

            if msg.op() == fdio::fdio_sys::ZXRIO_CLONE {
                if let Some(c) = reply_channel {
                    if let Ok(fdio_chan) = async::Channel::from_channel(c) {
                        self.serve_channel(fdio_chan);
                    }
                }
                return msg.into();
            }

            let service_channel = reply_channel.unwrap();
            let service_channel = async::Channel::from_channel(service_channel).unwrap();

            // TODO(raggi): re-arrange things to avoid the copy here
            let path = std::str::from_utf8(msg.data()).unwrap().to_owned();

            if path == "public" {
                self.serve_channel(service_channel);
                return msg.into();
            }

            match self.factories.iter_mut().find(|factory| factory.service_name() == path) {
                Some(factory) => factory.spawn_service(service_channel),
                None => eprintln!("No service found for path {}", path),
            }
            msg.into()
        }

        fn serve_channel(&mut self, chan: async::Channel) {
            let rmsg = chan.recv_msg(zx::MessageBuf::new());
            self.readers.push(rmsg);
        }
    }

    impl Future for FdioServer {
        type Output = Result<(), Error>;

        fn poll(mut self: PinMut<Self>, cx: &mut task::Context) -> Poll<Self::Output> {
            loop {
                match self.readers.poll_next_unpin(cx) {
                    Poll::Ready(Some(Ok((chan, buf)))) => {
                        let buf = self.dispatch(&chan, buf);
                        self.readers.push(chan.recv_msg(buf));
                    },
                    Poll::Ready(None) | Poll::Pending => return Poll::Pending,
                    Poll::Ready(Some(Err(_))) => {
                        // errors are ignored, as we assume that the channel should still be read from.
                    },
                }
            }
        }
    }
}
