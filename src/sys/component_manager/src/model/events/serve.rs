// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, FrameworkCapability},
        model::{
            error::ModelError,
            events::core::EventSource,
            events::registry::{Event, EventStream},
            hooks::{EventPayload, EventType},
            moniker::{AbsoluteMoniker, RelativeMoniker},
        },
    },
    async_trait::async_trait,
    fidl::endpoints::{create_request_stream, ClientEnd},
    fidl_fuchsia_test_events as fevents, fuchsia_async as fasync, fuchsia_trace as trace,
    fuchsia_zircon as zx,
    futures::{lock::Mutex, StreamExt, TryStreamExt},
    log::error,
    std::{path::PathBuf, sync::Arc},
};

pub async fn serve_event_source_sync(
    event_source: EventSource,
    stream: fevents::EventSourceSyncRequestStream,
) {
    let result = stream
        .try_for_each_concurrent(None, move |request| {
            let mut event_source = event_source.clone();
            async move {
                match request {
                    fevents::EventSourceSyncRequest::Subscribe {
                        event_types,
                        server_end,
                        responder,
                    } => {
                        // Convert the FIDL event types into standard event types
                        let event_types = event_types
                            .into_iter()
                            .map(|event_type| convert_fidl_event_type_to_std(event_type))
                            .collect();

                        // Subscribe to events.
                        let event_stream = event_source.subscribe(event_types).await;
                        let scope = event_source.scope();

                        // Unblock the component
                        responder.send()?;

                        // Serve the event_stream over FIDL asynchronously
                        let stream = server_end.into_stream()?;
                        serve_event_stream(
                            event_stream,
                            scope.unwrap_or(AbsoluteMoniker::root()),
                            stream,
                        )
                        .await?;
                    }
                    fevents::EventSourceSyncRequest::StartComponentTree { responder } => {
                        event_source.start_component_tree().await;
                        responder.send()?;
                    }
                }
                Ok(())
            }
        })
        .await;
    if let Err(e) = result {
        error!("Error serving EventSource: {}", e);
    }
}

/// Serves EventStream FIDL requests received over the provided stream.
async fn serve_event_stream(
    mut event_stream: EventStream,
    scope_moniker: AbsoluteMoniker,
    mut stream: fevents::EventStreamSyncRequestStream,
) -> Result<(), fidl::Error> {
    while let Some(Ok(fevents::EventStreamSyncRequest::Next { responder })) = stream.next().await {
        trace::duration!("component_manager", "events:fidl_get_next");
        // Wait for the next breakpoint to occur
        let event = event_stream.next().await;

        // Create the basic Event FIDL object.
        // This will begin serving the Handler protocol asynchronously.
        let event_fidl_object = create_event_fidl_object(&scope_moniker, event);

        // Respond with the Event FIDL object
        responder.send(event_fidl_object)?;
    }
    Ok(())
}

fn maybe_create_event_payload(
    scope: &AbsoluteMoniker,
    event_payload: EventPayload,
) -> Option<fevents::EventPayload> {
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
                    let scope_moniker =
                        scope_moniker.map(|a| RelativeMoniker::from_absolute(scope, &a));
                    fevents::CapabilitySource::Framework(fevents::FrameworkCapability {
                        scope_moniker: scope_moniker.map(|m| m.to_string()),
                        ..fevents::FrameworkCapability::empty()
                    })
                }
                CapabilitySource::Component { source_moniker, .. } => {
                    let source_moniker = RelativeMoniker::from_absolute(scope, &source_moniker);
                    fevents::CapabilitySource::Component(fevents::ComponentCapability {
                        source_moniker: Some(source_moniker.to_string()),
                        ..fevents::ComponentCapability::empty()
                    })
                }
                _ => return None,
            });

            let routing_payload =
                Some(fevents::RoutingPayload { routing_protocol, capability_id, source });
            Some(fevents::EventPayload { routing_payload, ..fevents::EventPayload::empty() })
        }
        _ => None,
    }
}

/// Creates the basic FIDL Event object containing the event type, target_realm
/// and basic handler for resumption.
fn create_event_fidl_object(scope_moniker: &AbsoluteMoniker, event: Event) -> fevents::Event {
    let event_type = Some(convert_std_event_type_to_fidl(event.event.payload.type_()));
    let target_relative_moniker =
        RelativeMoniker::from_absolute(scope_moniker, &event.event.target_moniker);
    let target_moniker = Some(target_relative_moniker.to_string());
    let event_payload = maybe_create_event_payload(scope_moniker, event.event.payload.clone());
    let handler = Some(serve_handler_async(event));
    fevents::Event { event_type, target_moniker, handler, event_payload }
}

/// Serves the server end of the RoutingProtocol FIDL protocol asynchronously.
fn serve_routing_protocol_async(
    capability_provider: Arc<Mutex<Option<Box<dyn CapabilityProvider>>>>,
) -> ClientEnd<fevents::RoutingProtocolMarker> {
    let (client_end, stream) = create_request_stream::<fevents::RoutingProtocolMarker>()
        .expect("failed to create request stream for RoutingProtocol");
    fasync::spawn(async move {
        serve_routing_protocol(capability_provider, stream).await;
    });
    client_end
}

/// Connects the component manager capability provider to
/// an external provider over FIDL
struct ExternalCapabilityProvider {
    proxy: fevents::CapabilityProviderProxy,
}

impl ExternalCapabilityProvider {
    pub fn new(client_end: ClientEnd<fevents::CapabilityProviderMarker>) -> Self {
        Self { proxy: client_end.into_proxy().expect("cannot create proxy from client_end") }
    }
}

#[async_trait]
impl CapabilityProvider for ExternalCapabilityProvider {
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: PathBuf,
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
    mut stream: fevents::RoutingProtocolRequestStream,
) {
    while let Some(Ok(request)) = stream.next().await {
        match request {
            fevents::RoutingProtocolRequest::SetProvider { client_end, responder } => {
                // Lock on the provider
                let mut capability_provider = capability_provider.lock().await;

                // Create an external provider and set it
                let external_provider = ExternalCapabilityProvider::new(client_end);
                *capability_provider = Some(Box::new(external_provider));

                responder.send().unwrap();
            }
            fevents::RoutingProtocolRequest::ReplaceAndOpen {
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

                // Unblock the interposer before opening the existing provider as the
                // existing provider may generate additional events which cannot be processed
                // until the interposer is unblocked.
                responder.send().unwrap();

                // Open the existing provider
                if let Some(existing_provider) = existing_provider {
                    // TODO(xbhatnag): We should be passing in the flags, mode and path
                    // to open the existing provider with. For a service, it doesn't matter
                    // but it would for other kinds of capabilities.
                    if let Err(e) = existing_provider.open(0, 0, PathBuf::new(), server_end).await {
                        panic!("Could not open existing provider -> {}", e);
                    }
                } else {
                    panic!("No provider set!");
                }
            }
        }
    }
}

/// Serves the server end of Handler FIDL protocol asynchronously
fn serve_handler_async(event: Event) -> ClientEnd<fevents::HandlerMarker> {
    let (client_end, mut stream) = create_request_stream::<fevents::HandlerMarker>()
        .expect("could not create request stream for handler protocol");
    fasync::spawn(async move {
        // Expect exactly one call to Resume
        if let Some(Ok(fevents::HandlerRequest::Resume { responder })) = stream.next().await {
            event.resume();
            responder.send().unwrap();
        }
    });
    client_end
}

fn convert_fidl_event_type_to_std(event_type: fevents::EventType) -> EventType {
    match event_type {
        fevents::EventType::AddDynamicChild => EventType::AddDynamicChild,
        fevents::EventType::BeforeStartInstance => EventType::BeforeStartInstance,
        fevents::EventType::PostDestroyInstance => EventType::PostDestroyInstance,
        fevents::EventType::PreDestroyInstance => EventType::PreDestroyInstance,
        fevents::EventType::ResolveInstance => EventType::ResolveInstance,
        fevents::EventType::RouteCapability => EventType::RouteCapability,
        fevents::EventType::StopInstance => EventType::StopInstance,
    }
}

fn convert_std_event_type_to_fidl(event_type: EventType) -> fevents::EventType {
    match event_type {
        EventType::AddDynamicChild => fevents::EventType::AddDynamicChild,
        EventType::BeforeStartInstance => fevents::EventType::BeforeStartInstance,
        EventType::PostDestroyInstance => fevents::EventType::PostDestroyInstance,
        EventType::PreDestroyInstance => fevents::EventType::PreDestroyInstance,
        EventType::ResolveInstance => fevents::EventType::ResolveInstance,
        EventType::RouteCapability => fevents::EventType::RouteCapability,
        EventType::StopInstance => fevents::EventType::StopInstance,
    }
}
