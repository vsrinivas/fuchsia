// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{capability::*, model::testing::breakpoints::*, model::*},
    fidl::endpoints::{create_request_stream, ClientEnd, ServerEnd},
    fidl_fuchsia_test_breakpoints as fbreak, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{future::BoxFuture, lock::Mutex, StreamExt},
    lazy_static::lazy_static,
    std::{convert::TryInto, sync::Arc},
};

lazy_static! {
    pub static ref BREAKPOINT_SYSTEM_SERVICE: cm_rust::CapabilityPath =
        "/svc/fuchsia.test.breakpoints.BreakpointSystem".try_into().unwrap();
}

/// A hook that provides the Breakpoints framework service to components.
pub struct BreakpointCapabilityHook {
    breakpoint_registry: Arc<BreakpointRegistry>,
}

impl BreakpointCapabilityHook {
    pub fn new(breakpoint_registry: Arc<BreakpointRegistry>) -> Self {
        Self { breakpoint_registry }
    }

    /// Creates and returns a BreakpointCapability when a component uses
    /// the Breakpoint framework service
    async fn on_route_framework_capability_async(
        self: Arc<Self>,
        capability_decl: &ComponentManagerCapability,
        capability: Option<Box<dyn ComponentManagerCapabilityProvider>>,
    ) -> Result<Option<Box<dyn ComponentManagerCapabilityProvider>>, ModelError> {
        match (capability, capability_decl) {
            (None, ComponentManagerCapability::ServiceProtocol(source_path))
                if *source_path == *BREAKPOINT_SYSTEM_SERVICE =>
            {
                return Ok(Some(Box::new(BreakpointCapabilityProvider::new(
                    self.breakpoint_registry.clone(),
                )) as Box<dyn ComponentManagerCapabilityProvider>))
            }
            (c, _) => return Ok(c),
        };
    }
}

impl Hook for BreakpointCapabilityHook {
    fn on(self: Arc<Self>, event: &Event) -> BoxFuture<'_, Result<(), ModelError>> {
        Box::pin(async move {
            if let EventPayload::RouteFrameworkCapability { capability, capability_provider } =
                &event.payload
            {
                let mut capability_provider = capability_provider.lock().await;
                *capability_provider = self
                    .on_route_framework_capability_async(&capability, capability_provider.take())
                    .await?;
            }
            Ok(())
        })
    }
}

/// Server end of Breakpoints framework service.
/// A component can use FIDL calls to register breakpoints and expect/resume invocations.
pub struct BreakpointCapabilityProvider {
    breakpoint_registry: Arc<BreakpointRegistry>,
}

impl BreakpointCapabilityProvider {
    pub fn new(breakpoint_registry: Arc<BreakpointRegistry>) -> Self {
        Self { breakpoint_registry }
    }

    /// Serves the BreakpointSystem FIDL service over the provided stream.
    /// A root_realm_created_receiver is provided when component manager
    /// is in debug mode.
    pub fn serve_async(
        &self,
        stream: fbreak::BreakpointSystemRequestStream,
        root_realm_created_receiver: Option<InvocationReceiver>,
    ) -> Result<(), ModelError> {
        let breakpoint_registry = self.breakpoint_registry.clone();
        fasync::spawn(async move {
            serve_system(stream, breakpoint_registry, root_realm_created_receiver).await;
        });
        Ok(())
    }
}

impl ComponentManagerCapabilityProvider for BreakpointCapabilityProvider {
    fn open(
        &self,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_chan: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>> {
        let stream = ServerEnd::<fbreak::BreakpointSystemMarker>::new(server_chan)
            .into_stream()
            .expect("could not convert channel into stream");
        Box::pin(async move { self.serve_async(stream, None) })
    }
}

/// Connects the component manager capability provider to
/// an external provider over FIDL
pub struct ExternalCapabilityProvider {
    proxy: fbreak::CapabilityProviderProxy,
}

impl ExternalCapabilityProvider {
    pub fn new(client_end: ClientEnd<fbreak::CapabilityProviderMarker>) -> Self {
        Self { proxy: client_end.into_proxy().expect("cannot create proxy from client_end") }
    }
}

impl ComponentManagerCapabilityProvider for ExternalCapabilityProvider {
    fn open(
        &self,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_chan: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async move {
            self.proxy
                .open(server_chan)
                .await
                .expect("failed to invoke CapabilityProvider::Open over FIDL");
            Ok(())
        })
    }
}

/// Serves BreakpointSystem FIDL requests received over the provided stream.
///
/// root_realm_receiver is provided when the "--debug" flag is passed into component manager.
/// When this receiver is provided, component manager has been halted on a RootRealmCreated event.
/// The first call to BreakpointSystem::StartComponentManager begins component manager's execution.
async fn serve_system(
    mut stream: fbreak::BreakpointSystemRequestStream,
    breakpoint_registry: Arc<BreakpointRegistry>,
    mut root_realm_created_receiver: Option<InvocationReceiver>,
) {
    while let Some(Ok(request)) = stream.next().await {
        match request {
            fbreak::BreakpointSystemRequest::Register { event_types, server_end, responder } => {
                serve_receiver_async(&breakpoint_registry, event_types, server_end).await;

                // Unblock the component
                responder.send().unwrap();
            }
            fbreak::BreakpointSystemRequest::StartComponentManager { responder } => {
                // Unblock component manager if the receiver is provided.
                if let Some(receiver) = root_realm_created_receiver {
                    // Get the next invocation (a RootComponentResolved event)
                    let invocation = receiver.receive().await;

                    // Resume from the invocation. This unblocks component manager.
                    invocation.resume();

                    // Ensure that StartComponentManager can only be called once.
                    root_realm_created_receiver = None;
                }
                responder.send().unwrap();
            }
        }
    }
}

/// Registers the given event types, then serves the server end of the
/// InvocationReceiver FIDL protocol asynchronously.
async fn serve_receiver_async(
    breakpoint_registry: &Arc<BreakpointRegistry>,
    event_types: Vec<fbreak::EventType>,
    server_end: ServerEnd<fbreak::InvocationReceiverMarker>,
) {
    // Convert the FIDL event types into standard event types
    let event_types = event_types
        .into_iter()
        .map(|event_type| convert_fidl_event_type_to_std(event_type))
        .collect();

    // Register the event types and get a receiver
    let receiver = breakpoint_registry.register(event_types).await;

    // Serve the InvocationReceiver FIDL protocol asynchronously
    let stream = server_end.into_stream().unwrap();
    fasync::spawn(async move {
        serve_receiver(receiver, stream).await;
    });
}

/// Serves InvocationReceiver FIDL requests received over the provided stream.
async fn serve_receiver(
    receiver: InvocationReceiver,
    mut stream: fbreak::InvocationReceiverRequestStream,
) {
    while let Some(Ok(fbreak::InvocationReceiverRequest::Next { responder })) = stream.next().await
    {
        // Wait for the next breakpoint to occur
        let invocation = receiver.receive().await;

        // Create the basic Invocation FIDL object.
        // This will begin serving the Handler protocol asynchronously.
        let invocation_fidl_object = create_invocation_fidl_object(invocation);

        // Respond with the Invocation FIDL object
        responder.send(invocation_fidl_object).unwrap();
    }
}

fn maybe_create_event_payload(event_payload: EventPayload) -> Option<fbreak::EventPayload> {
    match event_payload {
        EventPayload::RouteFrameworkCapability { capability, capability_provider, .. } => {
            let routing_protocol = Some(serve_routing_protocol_async(capability_provider));
            let capability = match capability {
                ComponentManagerCapability::Service(path) => path.to_string(),
                ComponentManagerCapability::ServiceProtocol(path) => path.to_string(),
                ComponentManagerCapability::Directory(path) => path.to_string(),
                ComponentManagerCapability::Runner(name) => name.to_string(),
            };
            let capability = Some(capability);
            let routing_payload = Some(fbreak::RoutingPayload { routing_protocol, capability });
            Some(fbreak::EventPayload { routing_payload, ..fbreak::EventPayload::empty() })
        }
        EventPayload::RouteBuiltinCapability { capability, capability_provider, .. } => {
            let routing_protocol = Some(serve_routing_protocol_async(capability_provider));
            let capability = match capability {
                ComponentManagerCapability::Service(path) => path.to_string(),
                ComponentManagerCapability::ServiceProtocol(path) => path.to_string(),
                ComponentManagerCapability::Directory(path) => path.to_string(),
                ComponentManagerCapability::Runner(name) => name.to_string(),
            };
            let capability = Some(capability);
            let routing_payload = Some(fbreak::RoutingPayload { routing_protocol, capability });
            Some(fbreak::EventPayload { routing_payload, ..fbreak::EventPayload::empty() })
        }
        EventPayload::UseCapability { use_, .. } => {
            let capability = use_
                .path()
                .expect("UseCapability only works with path-based capabilities")
                .to_string();
            let capability = Some(capability);
            let use_capability_payload = Some(fbreak::UseCapabilityPayload { capability });
            Some(fbreak::EventPayload { use_capability_payload, ..fbreak::EventPayload::empty() })
        }
        _ => None,
    }
}

/// Creates the basic FIDL Invocation object containing the event type, target_realm
/// and basic handler for resumption.
fn create_invocation_fidl_object(invocation: Invocation) -> fbreak::Invocation {
    let event_type = Some(convert_std_event_type_to_fidl(invocation.event.type_()));
    let target_moniker = Some(invocation.event.target_realm.abs_moniker.to_string());
    let event_payload = maybe_create_event_payload(invocation.event.payload.clone());
    let handler = Some(serve_handler_async(invocation));
    fbreak::Invocation { event_type, target_moniker, handler, event_payload }
}

/// Serves the server end of the RoutingProtocol FIDL protocol asynchronously.
fn serve_routing_protocol_async(
    capability_provider: Arc<Mutex<Option<Box<dyn ComponentManagerCapabilityProvider>>>>,
) -> ClientEnd<fbreak::RoutingProtocolMarker> {
    let (client_end, stream) = create_request_stream::<fbreak::RoutingProtocolMarker>()
        .expect("failed to create request stream for RoutingProtocol");
    fasync::spawn(async move {
        serve_routing_protocol(capability_provider, stream).await;
    });
    client_end
}

/// Serves RoutingProtocol FIDL requests received over the provided stream.
async fn serve_routing_protocol(
    capability_provider: Arc<Mutex<Option<Box<dyn ComponentManagerCapabilityProvider>>>>,
    mut stream: fbreak::RoutingProtocolRequestStream,
) {
    while let Some(Ok(fbreak::RoutingProtocolRequest::Route { client_end, responder })) =
        stream.next().await
    {
        // Create the external capability provider and set it
        let external_provider = ExternalCapabilityProvider::new(client_end);
        let mut capability_provider = capability_provider.lock().await;
        *capability_provider = Some(Box::new(external_provider));
        responder.send().unwrap();
    }
}

/// Serves the server end of Handler FIDL protocol asynchronously
fn serve_handler_async(invocation: Invocation) -> ClientEnd<fbreak::HandlerMarker> {
    let (client_end, mut stream) = create_request_stream::<fbreak::HandlerMarker>()
        .expect("could not create request stream for handler protocol");
    fasync::spawn(async move {
        // Expect exactly one call to Resume
        if let Some(Ok(fbreak::HandlerRequest::Resume { responder })) = stream.next().await {
            invocation.resume();
            responder.send().unwrap();
        }
    });
    client_end
}

fn convert_fidl_event_type_to_std(event_type: fbreak::EventType) -> EventType {
    match event_type {
        fbreak::EventType::AddDynamicChild => EventType::AddDynamicChild,
        fbreak::EventType::PostDestroyInstance => EventType::PostDestroyInstance,
        fbreak::EventType::PreDestroyInstance => EventType::PreDestroyInstance,
        fbreak::EventType::RootComponentResolved => EventType::RootComponentResolved,
        fbreak::EventType::RouteBuiltinCapability => EventType::RouteBuiltinCapability,
        fbreak::EventType::RouteFrameworkCapability => EventType::RouteFrameworkCapability,
        fbreak::EventType::StartInstance => EventType::StartInstance,
        fbreak::EventType::StopInstance => EventType::StopInstance,
        fbreak::EventType::UseCapability => EventType::UseCapability,
    }
}

fn convert_std_event_type_to_fidl(event_type: EventType) -> fbreak::EventType {
    match event_type {
        EventType::AddDynamicChild => fbreak::EventType::AddDynamicChild,
        EventType::PostDestroyInstance => fbreak::EventType::PostDestroyInstance,
        EventType::PreDestroyInstance => fbreak::EventType::PreDestroyInstance,
        EventType::RootComponentResolved => fbreak::EventType::RootComponentResolved,
        EventType::RouteBuiltinCapability => fbreak::EventType::RouteBuiltinCapability,
        EventType::RouteFrameworkCapability => fbreak::EventType::RouteFrameworkCapability,
        EventType::StartInstance => fbreak::EventType::StartInstance,
        EventType::StopInstance => fbreak::EventType::StopInstance,
        EventType::UseCapability => fbreak::EventType::UseCapability,
    }
}
