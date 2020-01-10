// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, FrameworkCapability},
        model::{
            breakpoints::core::ScopedBreakpointSystem,
            breakpoints::registry::{Invocation, InvocationReceiver},
            error::ModelError,
            hooks::{EventPayload, EventType},
            moniker::AbsoluteMoniker,
        },
    },
    async_trait::async_trait,
    fidl::endpoints::{create_request_stream, ClientEnd, ServerEnd},
    fidl_fuchsia_test_breakpoints as fbreak, fuchsia_async as fasync, fuchsia_trace as trace,
    fuchsia_zircon as zx,
    futures::{lock::Mutex, StreamExt},
    std::sync::Arc,
};

pub async fn serve_system(
    system: ScopedBreakpointSystem,
    mut stream: fbreak::BreakpointSystemRequestStream,
    mut root_instance_resolved_receiver: Option<InvocationReceiver>,
) {
    while let Some(Ok(request)) = stream.next().await {
        match request {
            fbreak::BreakpointSystemRequest::SetBreakpoints {
                event_types,
                server_end,
                responder,
            } => {
                // Convert the FIDL event types into standard event types
                let event_types = event_types
                    .into_iter()
                    .map(|event_type| convert_fidl_event_type_to_std(event_type))
                    .collect();

                // Set the breakpoints
                let receiver = system.set_breakpoints(event_types).await;

                // Serve the receiver over FIDL asynchronously
                fasync::spawn(async move {
                    serve_receiver(receiver, server_end).await;
                });

                // Unblock the component
                responder.send().unwrap();
            }
            fbreak::BreakpointSystemRequest::StartComponentTree { responder } => {
                // Unblock component manager if the receiver is provided.
                if let Some(mut receiver) = root_instance_resolved_receiver {
                    // Get the next invocation
                    let invocation = receiver.next().await;

                    // Ensure that this is a ResolveInstance event on the root instance
                    assert_eq!(EventType::ResolveInstance, invocation.event.payload.type_());
                    assert_eq!(AbsoluteMoniker::root(), invocation.event.target_moniker);

                    // Resume from the invocation. This unblocks component manager.
                    invocation.resume();

                    // Ensure that StartComponentTree can only be called once.
                    root_instance_resolved_receiver = None;
                }
                responder.send().unwrap();
            }
        }
    }
}

/// Serves InvocationReceiver FIDL requests received over the provided stream.
async fn serve_receiver(
    mut receiver: InvocationReceiver,
    server_end: ServerEnd<fbreak::InvocationReceiverMarker>,
) {
    // Serve the InvocationReceiver FIDL protocol asynchronously
    let mut stream = server_end.into_stream().unwrap();

    while let Some(Ok(fbreak::InvocationReceiverRequest::Next { responder })) = stream.next().await
    {
        trace::duration!("component_manager", "breakpoints:fidl_get_next");
        // Wait for the next breakpoint to occur
        let invocation = receiver.next().await;

        // Create the basic Invocation FIDL object.
        // This will begin serving the Handler protocol asynchronously.
        let invocation_fidl_object = create_invocation_fidl_object(invocation);

        // Respond with the Invocation FIDL object
        responder.send(invocation_fidl_object).unwrap();
    }
}

fn maybe_create_event_payload(event_payload: EventPayload) -> Option<fbreak::EventPayload> {
    match event_payload {
        EventPayload::RouteCapability { source, capability_provider, .. } => {
            let routing_protocol = Some(serve_routing_protocol_async(capability_provider));

            // Runners are special. They do not have a path, so their name is the capability ID.
            let capability_id = Some(
                if let CapabilitySource::Framework {
                    capability: FrameworkCapability::Runner(name),
                    ..
                } = &source
                {
                    name.to_string()
                } else if let Some(path) = source.path() {
                    path.to_string()
                } else {
                    return None;
                },
            );

            let source = Some(match source {
                CapabilitySource::Framework { scope_moniker, .. } => {
                    fbreak::CapabilitySource::Framework(fbreak::FrameworkCapability {
                        scope_moniker: scope_moniker.map(|m| m.to_string()),
                        ..fbreak::FrameworkCapability::empty()
                    })
                }
                CapabilitySource::Component { source_moniker, .. } => {
                    fbreak::CapabilitySource::Component(fbreak::ComponentCapability {
                        source_moniker: Some(source_moniker.to_string()),
                        ..fbreak::ComponentCapability::empty()
                    })
                }
                _ => return None,
            });

            let routing_payload =
                Some(fbreak::RoutingPayload { routing_protocol, capability_id, source });
            Some(fbreak::EventPayload { routing_payload, ..fbreak::EventPayload::empty() })
        }
        _ => None,
    }
}

/// Creates the basic FIDL Invocation object containing the event type, target_realm
/// and basic handler for resumption.
fn create_invocation_fidl_object(invocation: Invocation) -> fbreak::Invocation {
    let event_type = Some(convert_std_event_type_to_fidl(invocation.event.payload.type_()));
    let target_moniker = Some(invocation.event.target_moniker.to_string());
    let event_payload = maybe_create_event_payload(invocation.event.payload.clone());
    let handler = Some(serve_handler_async(invocation));
    fbreak::Invocation { event_type, target_moniker, handler, event_payload }
}

/// Serves the server end of the RoutingProtocol FIDL protocol asynchronously.
fn serve_routing_protocol_async(
    capability_provider: Arc<Mutex<Option<Box<dyn CapabilityProvider>>>>,
) -> ClientEnd<fbreak::RoutingProtocolMarker> {
    let (client_end, stream) = create_request_stream::<fbreak::RoutingProtocolMarker>()
        .expect("failed to create request stream for RoutingProtocol");
    fasync::spawn(async move {
        serve_routing_protocol(capability_provider, stream).await;
    });
    client_end
}

/// Connects the component manager capability provider to
/// an external provider over FIDL
struct ExternalCapabilityProvider {
    proxy: fbreak::CapabilityProviderProxy,
}

impl ExternalCapabilityProvider {
    pub fn new(client_end: ClientEnd<fbreak::CapabilityProviderMarker>) -> Self {
        Self { proxy: client_end.into_proxy().expect("cannot create proxy from client_end") }
    }
}

#[async_trait]
impl CapabilityProvider for ExternalCapabilityProvider {
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_chan: zx::Channel,
    ) -> Result<(), ModelError> {
        self.proxy
            .open(server_chan)
            .await
            .expect("failed to invoke CapabilityProvider::Open over FIDL");
        Ok(())
    }
}

/// Serves RoutingProtocol FIDL requests received over the provided stream.
async fn serve_routing_protocol(
    capability_provider: Arc<Mutex<Option<Box<dyn CapabilityProvider>>>>,
    mut stream: fbreak::RoutingProtocolRequestStream,
) {
    while let Some(Ok(request)) = stream.next().await {
        match request {
            fbreak::RoutingProtocolRequest::SetProvider { client_end, responder } => {
                // Lock on the provider
                let mut capability_provider = capability_provider.lock().await;

                // Create an external provider and set it
                let external_provider = ExternalCapabilityProvider::new(client_end);
                *capability_provider = Some(Box::new(external_provider));

                responder.send().unwrap();
            }
            fbreak::RoutingProtocolRequest::ReplaceAndOpen {
                client_end,
                server_end,
                responder,
            } => {
                // Lock on the provider
                let mut capability_provider = capability_provider.lock().await;

                // Take out the existing provider
                let existing_provider = capability_provider.take();

                // Create an external provider and set it
                let external_provider = ExternalCapabilityProvider::new(client_end);
                *capability_provider = Some(Box::new(external_provider));

                // Open the existing provider
                if let Some(existing_provider) = existing_provider {
                    // TODO(xbhatnag): We should be passing in the flags, mode and path
                    // to open the existing provider with. For a service, it doesn't matter
                    // but it would for other kinds of capabilities.
                    if let Err(e) = existing_provider.open(0, 0, String::new(), server_end).await {
                        panic!("Could not open existing provider -> {}", e);
                    }
                } else {
                    panic!("No provider set!");
                }
                responder.send().unwrap();
            }
        }
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
        fbreak::EventType::BeforeStartInstance => EventType::BeforeStartInstance,
        fbreak::EventType::PostDestroyInstance => EventType::PostDestroyInstance,
        fbreak::EventType::PreDestroyInstance => EventType::PreDestroyInstance,
        fbreak::EventType::ResolveInstance => EventType::ResolveInstance,
        fbreak::EventType::RouteCapability => EventType::RouteCapability,
        fbreak::EventType::StopInstance => EventType::StopInstance,
    }
}

fn convert_std_event_type_to_fidl(event_type: EventType) -> fbreak::EventType {
    match event_type {
        EventType::AddDynamicChild => fbreak::EventType::AddDynamicChild,
        EventType::BeforeStartInstance => fbreak::EventType::BeforeStartInstance,
        EventType::PostDestroyInstance => fbreak::EventType::PostDestroyInstance,
        EventType::PreDestroyInstance => fbreak::EventType::PreDestroyInstance,
        EventType::ResolveInstance => fbreak::EventType::ResolveInstance,
        EventType::RouteCapability => fbreak::EventType::RouteCapability,
        EventType::StopInstance => fbreak::EventType::StopInstance,
    }
}
