// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, FrameworkCapability},
        model::{
            error::ModelError,
            events::{
                event::{Event, SyncMode},
                source::EventSource,
                stream::EventStream,
            },
            hooks::EventPayload,
            moniker::{AbsoluteMoniker, RelativeMoniker},
        },
    },
    async_trait::async_trait,
    cm_rust::CapabilityName,
    fidl::endpoints::{create_request_stream, ClientEnd},
    fidl_fuchsia_component as fcomponent,
    fidl_fuchsia_io::{self as fio, NodeProxy},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_trace as trace,
    fuchsia_zircon as zx,
    futures::{lock::Mutex, StreamExt, TryStreamExt},
    log::{error, info, warn},
    std::{path::PathBuf, sync::Arc},
};

pub async fn serve_event_source_sync(
    event_source: EventSource,
    stream: fsys::BlockingEventSourceRequestStream,
) {
    let result = stream
        .try_for_each_concurrent(None, move |request| {
            let mut event_source = event_source.clone();
            async move {
                match request {
                    fsys::BlockingEventSourceRequest::Subscribe { events, stream, responder } => {
                        // Subscribe to events.
                        let events: Vec<CapabilityName> =
                            events.into_iter().map(|e| e.into()).collect();
                        match event_source.subscribe(events).await {
                            Ok(event_stream) => {
                                // Unblock the component
                                responder.send(&mut Ok(()))?;

                                // Serve the event_stream over FIDL asynchronously
                                serve_event_stream(event_stream, stream).await?;
                            }
                            Err(error) => {
                                info!("Error subscribing to events: {}", error);
                                responder.send(&mut Err(fcomponent::Error::ResourceUnavailable))?;
                            }
                        };
                    }
                    fsys::BlockingEventSourceRequest::StartComponentTree { responder } => {
                        event_source.start_component_tree().await;
                        responder.send()?;
                    }
                }
                Ok(())
            }
        })
        .await;
    if let Err(e) = result {
        error!("Error serving BlockingEventSource: {}", e);
    }
}

/// Serves EventStream FIDL requests received over the provided stream.
async fn serve_event_stream(
    mut event_stream: EventStream,
    client_end: ClientEnd<fsys::EventStreamMarker>,
) -> Result<(), fidl::Error> {
    let listener = client_end.into_proxy().expect("cannot create proxy from client_end");
    while let Some(event) = event_stream.next().await {
        trace::duration!("component_manager", "events:fidl_get_next");
        // Create the basic Event FIDL object.
        // This will begin serving the Handler protocol asynchronously.
        let event_fidl_object = create_event_fidl_object(event)?;

        if let Err(e) = listener.on_event(event_fidl_object) {
            // It's not an error for the client to drop the listener.
            if !e.is_closed() {
                warn!("Unexpected error while serving EventStream: {:?}", e);
            }
            break;
        }
    }
    Ok(())
}

fn maybe_create_event_payload(
    scope: &AbsoluteMoniker,
    event_payload: &EventPayload,
) -> Result<Option<fsys::EventPayload>, fidl::Error> {
    match event_payload {
        EventPayload::CapabilityReady { path, node } => {
            maybe_create_capability_ready_payload(path.to_string(), node)
        }
        EventPayload::CapabilityRouted { source, capability_provider, .. } => {
            Ok(maybe_create_capability_routed_payload(scope, source, capability_provider.clone()))
        }
        _ => Ok(None),
    }
}

fn maybe_create_capability_ready_payload(
    path: String,
    node: &NodeProxy,
) -> Result<Option<fsys::EventPayload>, fidl::Error> {
    let (node_clone, server_end) = fidl::endpoints::create_proxy()?;
    node.clone(fio::CLONE_FLAG_SAME_RIGHTS, server_end)?;
    let node_client_end = node_clone
        .into_channel()
        .expect("could not convert directory to channel")
        .into_zx_channel()
        .into();
    let payload = fsys::CapabilityReadyPayload { path: Some(path), node: Some(node_client_end) };
    Ok(Some(fsys::EventPayload { capability_ready: Some(payload), ..fsys::EventPayload::empty() }))
}

fn maybe_create_capability_routed_payload(
    scope: &AbsoluteMoniker,
    source: &CapabilitySource,
    capability_provider: Arc<Mutex<Option<Box<dyn CapabilityProvider>>>>,
) -> Option<fsys::EventPayload> {
    let routing_protocol = Some(serve_routing_protocol_async(capability_provider));

    // Runners are special. They do not have a path, so their name is the capability ID.
    let capability_id = Some(
        if let CapabilitySource::Framework {
            capability: FrameworkCapability::Runner(name), ..
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
                scope_moniker.as_ref().map(|a| RelativeMoniker::from_absolute(scope, &a));
            fsys::CapabilitySource::Framework(fsys::FrameworkCapability {
                scope_moniker: scope_moniker.as_ref().map(|m| m.to_string()),
                ..fsys::FrameworkCapability::empty()
            })
        }
        CapabilitySource::Component { realm, .. } => {
            let realm = realm.upgrade().ok()?;
            let source_moniker = RelativeMoniker::from_absolute(scope, &realm.abs_moniker);
            fsys::CapabilitySource::Component(fsys::ComponentCapability {
                source_moniker: Some(source_moniker.to_string()),
                ..fsys::ComponentCapability::empty()
            })
        }
        _ => return None,
    });

    let routing_payload = Some(fsys::RoutingPayload { routing_protocol, capability_id, source });
    Some(fsys::EventPayload { routing_payload, ..fsys::EventPayload::empty() })
}

/// Creates the basic FIDL Event object containing the event type, target_realm
/// and basic handler for resumption.
fn create_event_fidl_object(event: Event) -> Result<fsys::Event, fidl::Error> {
    let event_type = Some(event.event.payload.type_().into());
    let target_relative_moniker =
        RelativeMoniker::from_absolute(&event.scope_moniker, &event.event.target_moniker);
    let target_moniker = Some(target_relative_moniker.to_string());
    let event_payload = maybe_create_event_payload(&event.scope_moniker, &event.event.payload)?;
    let handler = maybe_serve_handler_async(event);
    Ok(fsys::Event { event_type, target_moniker, handler, event_payload })
}

/// Serves the server end of the RoutingProtocol FIDL protocol asynchronously.
fn serve_routing_protocol_async(
    capability_provider: Arc<Mutex<Option<Box<dyn CapabilityProvider>>>>,
) -> ClientEnd<fsys::RoutingProtocolMarker> {
    let (client_end, stream) = create_request_stream::<fsys::RoutingProtocolMarker>()
        .expect("failed to create request stream for RoutingProtocol");
    fasync::spawn(async move {
        serve_routing_protocol(capability_provider, stream).await;
    });
    client_end
}

/// Connects the component manager capability provider to
/// an external provider over FIDL
struct ExternalCapabilityProvider {
    proxy: fsys::CapabilityProviderProxy,
}

impl ExternalCapabilityProvider {
    pub fn new(client_end: ClientEnd<fsys::CapabilityProviderMarker>) -> Self {
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
    mut stream: fsys::RoutingProtocolRequestStream,
) {
    while let Some(Ok(request)) = stream.next().await {
        match request {
            fsys::RoutingProtocolRequest::SetProvider { client_end, responder } => {
                // Lock on the provider
                let mut capability_provider = capability_provider.lock().await;

                // Create an external provider and set it
                let external_provider = ExternalCapabilityProvider::new(client_end);
                *capability_provider = Some(Box::new(external_provider));

                responder.send().unwrap();
            }
            fsys::RoutingProtocolRequest::ReplaceAndOpen { client_end, server_end, responder } => {
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
fn maybe_serve_handler_async(event: Event) -> Option<ClientEnd<fsys::HandlerMarker>> {
    if event.sync_mode() == SyncMode::Async {
        return None;
    }
    let (client_end, mut stream) = create_request_stream::<fsys::HandlerMarker>()
        .expect("could not create request stream for handler protocol");
    fasync::spawn(async move {
        // Expect exactly one call to Resume
        if let Some(Ok(fsys::HandlerRequest::Resume { responder })) = stream.next().await {
            event.resume();
            responder.send().unwrap();
        }
    });
    Some(client_end)
}
