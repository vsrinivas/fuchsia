// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::webdriver::types::{EnableDevToolsResult, GetDevToolsPortsResult};
use anyhow::{format_err, Error};
use fidl::endpoints::{create_proxy, create_request_stream, ClientEnd, ServerEnd, ServiceMarker};
use fidl_fuchsia_web::{
    ContextMarker, ContextProviderMarker, ContextProviderProxy, CreateContextParams, DebugMarker,
    DebugProxy, DevToolsListenerMarker, DevToolsListenerRequest, DevToolsListenerRequestStream,
    DevToolsPerContextListenerMarker, DevToolsPerContextListenerRequest,
    DevToolsPerContextListenerRequestStream, FrameMarker, NavigationControllerMarker,
};
use fuchsia_async as fasync;
use fuchsia_component as app;
use fuchsia_syslog::macros::*;
use fuchsia_zircon as zx;
use futures::channel::mpsc;
use futures::prelude::*;
use glob::glob;
use parking_lot::Mutex;
use std::collections::HashSet;
use std::iter::FromIterator;
use std::ops::DerefMut;

/// Facade providing access to WebDriver debug services.  Supports enabling
/// DevTools ports on Chrome contexts, and retrieving the set of open ports.
/// Open ports can be used by Chromedriver to manipulate contexts for testing.
#[derive(Debug)]
pub struct WebdriverFacade {
    /// Internal facade, instantiated when facade is initialized with
    /// `enable_dev_tools`.
    internal: Mutex<Option<WebdriverFacadeInternal>>,
}

impl WebdriverFacade {
    /// Create a new `WebdriverFacade`
    pub fn new() -> WebdriverFacade {
        WebdriverFacade { internal: Mutex::new(None) }
    }

    /// Configure WebDriver to start any future contexts in debug mode.  This
    /// allows contexts to be controlled remotely through ChromeDriver.
    pub async fn enable_dev_tools(&self) -> Result<EnableDevToolsResult, Error> {
        let mut internal = self.internal.lock();
        if internal.is_none() {
            let initialized_internal = WebdriverFacadeInternal::new().await?;
            internal.replace(initialized_internal);
            Ok(EnableDevToolsResult::Success)
        } else {
            Err(format_err!("DevTools already enabled."))
        }
    }

    /// Returns a list of open DevTools ports.  Returns an error if DevTools
    /// have not been enabled using `enable_dev_tools`.
    pub async fn get_dev_tools_ports(&self) -> Result<GetDevToolsPortsResult, Error> {
        let mut internal = self.internal.lock();
        match internal.deref_mut() {
            Some(facade) => Ok(GetDevToolsPortsResult::new(facade.get_ports())),
            None => Err(format_err!("DevTools are not enabled.")),
        }
    }
}

/// Internal struct providing updated list of open DevTools ports using
/// WebDriver Debug service.
#[derive(Debug)]
struct WebdriverFacadeInternal {
    /// Set of currently open DevTools ports.
    dev_tools_ports: HashSet<u16>,
    /// Receiving end for port update channel.
    port_update_receiver: mpsc::UnboundedReceiver<PortUpdateMessage>,
    /// Proxy to ContextProvider that is held to ensure WebDriver is running
    /// for the duration of the test.
    _context_provider: ContextProviderProxy,
}

impl WebdriverFacadeInternal {
    /// Create a new `WebdriverFacadeInternal`.  Can fail if connecting to the
    /// debug service fails.
    pub async fn new() -> Result<WebdriverFacadeInternal, Error> {
        let context_provider = Self::get_context_provider_proxy().await?;
        let port_update_receiver = Self::get_port_event_receiver().await?;
        Ok(WebdriverFacadeInternal {
            dev_tools_ports: HashSet::new(),
            port_update_receiver,
            _context_provider: context_provider,
        })
    }

    /// Returns a copy of the available ports.
    pub fn get_ports(&mut self) -> Vec<u16> {
        self.update_port_set();
        Vec::from_iter(self.dev_tools_ports.iter().cloned())
    }

    /// Consumes messages produced by context listeners to update set of open ports.
    fn update_port_set(&mut self) {
        while let Ok(Some(update)) = self.port_update_receiver.try_next() {
            match update {
                PortUpdateMessage::PortOpened(port) => self.dev_tools_ports.insert(port),
                PortUpdateMessage::PortClosed(port) => self.dev_tools_ports.remove(&port),
            };
        }
    }

    /// Returns a proxy to ContextProvider.  Completion of this method
    /// guarantees that the debug service is published on the Hub.
    async fn get_context_provider_proxy() -> Result<ContextProviderProxy, Error> {
        // Walk through creating a frame and navigation controller.  Completion
        // of NavigationController GetVisibleEntry guarantees that the debug
        // service is ready to accept requests.
        let context_provider = app::client::connect_to_service::<ContextProviderMarker>()?;
        let (context_proxy, context_server_end) = create_proxy::<ContextMarker>()?;

        let (client, server) = zx::Channel::create()?;
        fdio::service_connect("/svc", server)?;
        let service_directory = ClientEnd::new(client);

        context_provider.create(
            CreateContextParams {
                service_directory: Some(service_directory),
                ..CreateContextParams::empty()
            },
            context_server_end,
        )?;

        let (frame_proxy, frame_server_end) = create_proxy::<FrameMarker>()?;
        context_proxy.create_frame(frame_server_end)?;

        let (navigation_proxy, navigation_server_end) =
            create_proxy::<NavigationControllerMarker>()?;
        frame_proxy.get_navigation_controller(navigation_server_end)?;
        navigation_proxy.get_visible_entry().await?;
        Ok(context_provider)
    }

    /// Setup a channel to receive port open/close channels and return the
    /// receiving end.  Assumes Webdriver is already running.
    async fn get_port_event_receiver() -> Result<mpsc::UnboundedReceiver<PortUpdateMessage>, Error>
    {
        let debug_proxy = Self::get_debug_proxy().await?;
        let (dev_tools_client, dev_tools_stream) =
            create_request_stream::<DevToolsListenerMarker>()?;
        let (port_update_sender, port_update_receiver) = mpsc::unbounded();

        Self::spawn_dev_tools_listener(dev_tools_stream, port_update_sender);
        debug_proxy.enable_dev_tools(dev_tools_client).await?;
        Ok(port_update_receiver)
    }

    /// Get a client to the WebDriver Debug service published in the Hub.
    async fn get_debug_proxy() -> Result<DebugProxy, Error> {
        let found_path = Self::get_path_to_debug().await?;
        let (client, server) = zx::Channel::create()?;
        fdio::service_connect(found_path.as_ref(), server)?;
        Ok(DebugProxy::new(fasync::Channel::from_channel(client)?))
    }

    /// Locate debug service published in the Hub.
    async fn get_path_to_debug() -> Result<String, Error> {
        // Find debug service in the Hub.
        let glob_query = format!("/hub/c/context_provider.cmx/*/out/debug/{}", DebugMarker::NAME);
        if let Some(found_path) = glob(&glob_query)?.filter_map(|entry| entry.ok()).next() {
            Ok(found_path.to_string_lossy().to_string())
        } else {
            Err(format_err!("Failed to find debug service."))
        }
    }

    /// Spawn an instance of `DevToolsListener` that forwards port open/close
    /// events from the debug request channel to the mpsc channel.
    fn spawn_dev_tools_listener(
        dev_tools_request_stream: DevToolsListenerRequestStream,
        port_update_sender: mpsc::UnboundedSender<PortUpdateMessage>,
    ) {
        fasync::Task::spawn(async move {
            let dev_tools_listener = DevToolsListener::new(port_update_sender);
            dev_tools_listener
                .handle_requests_from_stream(dev_tools_request_stream)
                .await
                .unwrap_or_else(|_| print!("Error handling DevToolsListener channel!"));
        })
        .detach();
    }
}

/// Message passed from a `DevToolsPerContextListener` to
/// `WebdriverFacade` to notify it of a port opening or closing
#[derive(Debug)]
enum PortUpdateMessage {
    /// Sent when a port is opened.
    PortOpened(u16),
    /// Sent when a port is closed.
    PortClosed(u16),
}

/// An implementation of `fuchsia.web.DevToolsListener` that instantiates
/// `DevToolsPerContextListener` when a context is created.
struct DevToolsListener {
    /// Sender end of port update channel.
    port_update_sender: mpsc::UnboundedSender<PortUpdateMessage>,
}

impl DevToolsListener {
    /// Create a new `DevToolsListener`
    fn new(port_update_sender: mpsc::UnboundedSender<PortUpdateMessage>) -> Self {
        DevToolsListener { port_update_sender }
    }

    /// Handle requests made to `DevToolsListener`.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: DevToolsListenerRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            let DevToolsListenerRequest::OnContextDevToolsAvailable { listener, .. } = request;
            self.on_context_created(listener)?;
        }
        Ok(())
    }

    /// Handles OnContextDevToolsAvailable.  Spawns an instance of
    /// `DevToolsPerContextListener` to handle the new Chrome context.
    fn on_context_created(
        &self,
        listener: ServerEnd<DevToolsPerContextListenerMarker>,
    ) -> Result<(), Error> {
        fx_log_info!("Chrome context created");
        let listener_request_stream = listener.into_stream()?;
        let port_update_sender = mpsc::UnboundedSender::clone(&self.port_update_sender);
        fasync::Task::spawn(async move {
            let mut per_context_listener = DevToolsPerContextListener::new(port_update_sender);
            per_context_listener
                .handle_requests_from_stream(listener_request_stream)
                .await
                .unwrap_or_else(|_| fx_log_warn!("Error handling DevToolsListener channel!"));
        })
        .detach();
        Ok(())
    }
}

/// An implementation of `fuchsia.web.DevToolsPerContextListener` that forwards
/// port open/close events to an mpsc channel.
struct DevToolsPerContextListener {
    /// Sender end of port update channel.
    port_update_sender: mpsc::UnboundedSender<PortUpdateMessage>,
}

impl DevToolsPerContextListener {
    /// Create a new `DevToolsPerContextListener`
    fn new(port_update_sender: mpsc::UnboundedSender<PortUpdateMessage>) -> Self {
        DevToolsPerContextListener { port_update_sender }
    }

    /// Handle requests made to `DevToolsPerContextListener`.  The HTTP port
    /// becomes available when OnHttpPortOpen is called, and becomes
    /// unavailable when the stream closes.
    pub async fn handle_requests_from_stream(
        &mut self,
        mut stream: DevToolsPerContextListenerRequestStream,
    ) -> Result<(), Error> {
        let mut context_port = None;

        while let Ok(Some(request)) = stream.try_next().await {
            let DevToolsPerContextListenerRequest::OnHttpPortOpen { port, .. } = request;
            context_port.replace(port);
            self.on_port_open(port)?;
        }

        // Port is closed after stream ends.
        if let Some(port) = context_port {
            self.on_port_closed(port)?;
        }
        Ok(())
    }

    /// Send a port open event.
    fn on_port_open(&mut self, port: u16) -> Result<(), Error> {
        fx_log_info!("DevTools port {:?} opened", port);
        self.port_update_sender
            .unbounded_send(PortUpdateMessage::PortOpened(port))
            .map_err(|_| format_err!("Error sending port open message"))
    }

    /// Send a port close event.
    fn on_port_closed(&mut self, port: u16) -> Result<(), Error> {
        fx_log_info!("DevTools port {:?} closed", port);
        self.port_update_sender
            .unbounded_send(PortUpdateMessage::PortClosed(port))
            .map_err(|_| format_err!("Error sending port closed message"))
    }
}
