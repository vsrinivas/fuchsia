// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::events::{
        CapabilityRouted, Event, EventMatcher, EventSource, EventStreamError, RoutingProtocol,
    },
    anyhow::Error,
    async_trait::async_trait,
    fidl::endpoints::{create_request_stream, ClientEnd, ServerEnd, ServiceMarker},
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    futures::StreamExt,
    io_util::node::connect_in_namespace,
    log::warn,
    std::sync::Arc,
    vfs::{
        directory::{
            entry::DirectoryEntry, immutable::connection::io1::ImmutableConnection, simple::Simple,
        },
        execution_scope::ExecutionScope,
        path::Path,
    },
};

#[async_trait]
pub trait CapabilityInjector: 'static + Send + Sync {
    async fn inject(self: &Arc<Self>, event_source: &EventSource, matcher: EventMatcher) {
        self.subscribe(event_source, matcher).await;
    }

    async fn subscribe(self: &Arc<Self>, event_source: &EventSource, matcher: EventMatcher) {
        let mut event_stream = event_source
            .subscribe(vec![CapabilityRouted::NAME])
            .await
            .expect("Could not create event stream");

        // Spawn a new task to listen to CapabilityRoutedEvents
        let injector = self.clone();
        fasync::Task::spawn(async move {
            loop {
                // Wait for a capability routed event that matches
                let event = match event_stream
                    .wait_until_exact::<CapabilityRouted>(matcher.clone())
                    .await
                {
                    Ok(e) => e,
                    Err(e) => match e.downcast::<EventStreamError>() {
                        Ok(EventStreamError::StreamClosed) => return,
                        Err(e) => panic!("Unknown error! {:?}", e),
                    },
                };

                // An event was found! Inject the route.
                if event.result.is_ok() {
                    let provider_client_end = injector.clone().route();
                    event
                        .protocol_proxy()
                        .expect("Event does not have routing protocol")
                        .set_provider(provider_client_end)
                        .await
                        .expect("Could not set provider for CapabilityRouted event");
                }
            }
        })
        .detach();
    }

    fn route(self: Arc<Self>) -> ClientEnd<fsys::CapabilityProviderMarker> {
        // Create the CapabilityProvider channel
        let (provider_client_end, mut provider_capability_stream) =
            create_request_stream::<fsys::CapabilityProviderMarker>()
                .expect("Could not create request stream for CapabilityProvider");

        // Spawn a task to handle this new route
        fasync::Task::spawn(async move {
            while let Some(Ok(fsys::CapabilityProviderRequest::Open {
                server_end,
                flags,
                mode,
                path,
                responder,
            })) = provider_capability_stream.next().await
            {
                // Unblock component manager
                responder.send().expect("Failed to respond to CapabilityProvider Open");

                // Spawn a task to serve the capability
                let injector = self.clone();
                fasync::Task::spawn(async move {
                    injector.open(server_end, flags, mode, path).await;
                })
                .detach();
            }
        })
        .detach();

        provider_client_end
    }

    // Implement this method to serve the capability on this route
    async fn open(self: Arc<Self>, server_end: fidl::Channel, flags: u32, mode: u32, path: String);
}

/// Allows tests to inject pseudo-filesystems for capabilities.
/// This can be used to serve directory capabilities like /dev
/// and /temp by hosting the filesystem within the test itself.
pub struct DirectoryInjector {
    dir: Arc<Simple<ImmutableConnection>>,
    scope: ExecutionScope,
}

impl DirectoryInjector {
    pub fn new(dir: Arc<Simple<ImmutableConnection>>) -> Arc<Self> {
        Arc::new(Self { dir, scope: ExecutionScope::new() })
    }

    pub async fn wait(self) {
        self.scope.wait().await;
    }
}

#[async_trait]
impl CapabilityInjector for DirectoryInjector {
    async fn open(self: Arc<Self>, server_end: fidl::Channel, flags: u32, mode: u32, path: String) {
        let mode_type = mode & fio::MODE_TYPE_MASK;
        if mode_type != fio::MODE_TYPE_DIRECTORY {
            warn!("Injected directory received unexpected mode type: 0x{:x}", mode_type);
        }

        let server_end = ServerEnd::from(server_end);
        let dir = self.dir.clone();
        let scope = self.scope.clone();
        let relative_path =
            Path::validate_and_split(path).expect("Relative path could not be validated by VFS.");
        dir.open(scope, flags, mode, relative_path, server_end);
    }
}

/// Allows tests to inject capabilities from the test namespace.
/// This can be used to serve capabilities that are difficult to
/// mock within a test (such as Vulkan).
pub struct TestNamespaceInjector {
    path_in_test_namespace: &'static str,
}

impl TestNamespaceInjector {
    pub fn new(path_in_test_namespace: &'static str) -> Arc<Self> {
        Arc::new(Self { path_in_test_namespace })
    }
}

#[async_trait]
impl CapabilityInjector for TestNamespaceInjector {
    async fn open(
        self: Arc<Self>,
        server_end: fidl::Channel,
        flags: u32,
        _mode: u32,
        path: String,
    ) {
        if !path.is_empty() {
            warn!("TestNamespaceInjector received non-empty relative path: \"{}\"", path);
        }

        if flags & !(fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE) != 0 {
            warn!("TestNamespaceInjector received unexpected flags: 0x{:x}", flags);
        }

        connect_in_namespace(self.path_in_test_namespace, flags, server_end)
            .expect("Could not connect to capability in test namespace");
    }
}

/// A ProtocolInjector allows a test to implement a protocol to be used by a component.
///
/// Client <---> ProtocolInjector
#[async_trait]
pub trait ProtocolInjector {
    type Marker: ServiceMarker;

    /// This function will be run in a spawned task when a client attempts
    /// to connect to the service being injected. `request_stream` is a stream of
    /// requests coming in from the client.
    async fn serve(
        self: Arc<Self>,
        mut request_stream: <<Self as ProtocolInjector>::Marker as ServiceMarker>::RequestStream,
    ) -> Result<(), Error>;
}

#[async_trait]
impl<M: ServiceMarker, T: ProtocolInjector<Marker = M> + 'static + Sync + Send> CapabilityInjector
    for T
{
    async fn inject(self: &Arc<Self>, event_source: &EventSource, matcher: EventMatcher) {
        let matcher = matcher.expect_capability_id(M::NAME);
        CapabilityInjector::subscribe(self, event_source, matcher).await;
    }

    async fn open(self: Arc<Self>, server_end: fidl::Channel, flags: u32, mode: u32, path: String) {
        if !path.is_empty() {
            warn!("Injected protocol {} received a non-empty path: {}", M::NAME, path);
        }

        if flags != (fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE) {
            warn!("Injected protocol {} received unexpected flags: 0x{:x}", M::NAME, flags);
        }

        let mode_type = mode & fio::MODE_TYPE_MASK;
        if mode_type != fio::MODE_TYPE_SERVICE {
            warn!("Injected protocol {} received unexpected mode type: 0x{:x}", M::NAME, mode_type);
        }

        // Create the stream for the Client <---> Injector connection
        let stream = ServerEnd::<M>::new(server_end)
            .into_stream()
            .expect("could not convert channel into stream");

        self.serve(stream).await.expect("Injection failed");
    }
}
