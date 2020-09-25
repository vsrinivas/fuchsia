// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource},
        channel,
        model::{
            error::ModelError,
            events::{
                event::{Event, SyncMode},
                source::EventSource,
                stream::EventStream,
            },
            hooks::{
                EventError, EventErrorPayload, EventPayload, EventResult, EventType, HasEventType,
            },
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
    log::{debug, error, info, warn},
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
pub async fn serve_event_stream(
    mut event_stream: EventStream,
    client_end: ClientEnd<fsys::EventStreamMarker>,
) -> Result<(), fidl::Error> {
    let listener = client_end.into_proxy().expect("cannot create proxy from client_end");
    while let Some(event) = event_stream.next().await {
        trace::duration!("component_manager", "events:fidl_get_next");
        // Create the basic Event FIDL object.
        // This will begin serving the Handler protocol asynchronously.
        let event_fidl_object = create_event_fidl_object(event).await?;

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

async fn maybe_create_event_result(
    scope: &AbsoluteMoniker,
    event_result: &EventResult,
) -> Result<Option<fsys::EventResult>, fidl::Error> {
    match event_result {
        Ok(EventPayload::CapabilityReady { path, node, .. }) => {
            Ok(Some(create_capability_ready_payload(path.to_string(), node)?))
        }
        Ok(EventPayload::CapabilityRequested { path, capability, .. }) => Ok(Some(
            create_capability_requested_payload(path.to_string(), capability.clone()).await,
        )),
        Ok(EventPayload::CapabilityRouted { source, capability_provider, .. }) => {
            Ok(maybe_create_capability_routed_payload(scope, source, capability_provider.clone()))
        }
        Ok(EventPayload::Running { started_timestamp }) => {
            Ok(Some(fsys::EventResult::Payload(fsys::EventPayload::Running(
                fsys::RunningPayload { started_timestamp: Some(started_timestamp.into_nanos()) },
            ))))
        }
        Ok(EventPayload::Stopped { status }) => Ok(Some(fsys::EventResult::Payload(
            fsys::EventPayload::Stopped(fsys::StoppedPayload { status: Some(status.into_raw()) }),
        ))),
        Ok(payload) => Ok(maybe_create_empty_payload(payload.event_type())),
        Err(EventError {
            source,
            event_error_payload: EventErrorPayload::CapabilityReady { path },
        }) => Ok(Some(fsys::EventResult::Error(fsys::EventError {
            error_payload: Some(fsys::EventErrorPayload::CapabilityReady(
                fsys::CapabilityReadyError { path: Some(path.to_string()) },
            )),
            description: Some(format!("{}", source)),
            ..fsys::EventError::empty()
        }))),
        Err(EventError {
            source,
            event_error_payload: EventErrorPayload::CapabilityRequested { path, .. },
        }) => Ok(Some(fsys::EventResult::Error(fsys::EventError {
            error_payload: Some(fsys::EventErrorPayload::CapabilityRequested(
                fsys::CapabilityRequestedError { path: Some(path.to_string()) },
            )),
            description: Some(format!("{}", source)),
            ..fsys::EventError::empty()
        }))),
        Err(EventError {
            source,
            event_error_payload: EventErrorPayload::Running { started_timestamp },
        }) => Ok(Some(fsys::EventResult::Error(fsys::EventError {
            error_payload: Some(fsys::EventErrorPayload::Running(fsys::RunningError {
                started_timestamp: Some(started_timestamp.into_nanos()),
            })),
            description: Some(format!("{}", source)),
            ..fsys::EventError::empty()
        }))),
        Err(error) => Ok(maybe_create_empty_error_payload(error)),
    }
}

fn create_capability_ready_payload(
    path: String,
    node: &NodeProxy,
) -> Result<fsys::EventResult, fidl::Error> {
    let node = {
        let (node_clone, server_end) = fidl::endpoints::create_proxy()?;
        node.clone(fio::CLONE_FLAG_SAME_RIGHTS, server_end)?;
        let node_client_end = node_clone
            .into_channel()
            .expect("could not convert directory to channel")
            .into_zx_channel()
            .into();
        Some(node_client_end)
    };

    let payload = fsys::CapabilityReadyPayload { path: Some(path), node };
    Ok(fsys::EventResult::Payload(fsys::EventPayload::CapabilityReady(payload)))
}

async fn create_capability_requested_payload(
    path: String,
    capability: Arc<Mutex<Option<zx::Channel>>>,
) -> fsys::EventResult {
    let capability = capability.lock().await.take();
    match capability {
        Some(capability) => {
            let payload =
                fsys::CapabilityRequestedPayload { path: Some(path), capability: Some(capability) };
            fsys::EventResult::Payload(fsys::EventPayload::CapabilityRequested(payload))
        }
        None => {
            // This can happen if a hook takes the capability prior to the events system.
            let payload =
                fsys::EventErrorPayload::CapabilityRequested(fsys::CapabilityRequestedError {
                    path: Some(path),
                });
            fsys::EventResult::Error(fsys::EventError {
                error_payload: Some(payload),
                description: Some("Capability unavailable".to_string()),
                ..fsys::EventError::empty()
            })
        }
    }
}

fn maybe_create_capability_routed_payload(
    scope: &AbsoluteMoniker,
    source: &CapabilitySource,
    capability_provider: Arc<Mutex<Option<Box<dyn CapabilityProvider>>>>,
) -> Option<fsys::EventResult> {
    let routing_protocol = Some(serve_routing_protocol_async(capability_provider));

    let capability_id = Some(source.id());
    let source = Some(match source {
        CapabilitySource::Component { realm, .. } => {
            let realm = realm.upgrade().ok()?;
            let source_moniker = RelativeMoniker::from_absolute(scope, &realm.abs_moniker);
            fsys::CapabilitySource::Component(fsys::ComponentCapability {
                source_moniker: Some(source_moniker.to_string()),
                ..fsys::ComponentCapability::empty()
            })
        }
        CapabilitySource::Framework { scope_moniker, .. } => {
            let scope_moniker = RelativeMoniker::from_absolute(scope, &scope_moniker);
            fsys::CapabilitySource::Framework(fsys::FrameworkCapability {
                scope_moniker: Some(scope_moniker.to_string()),
                ..fsys::FrameworkCapability::empty()
            })
        }
        CapabilitySource::Builtin { .. } | CapabilitySource::Namespace { .. } => {
            fsys::CapabilitySource::AboveRoot(fsys::AboveRootCapability {
                ..fsys::AboveRootCapability::empty()
            })
        }
    });

    let payload = fsys::CapabilityRoutedPayload { routing_protocol, capability_id, source };
    Some(fsys::EventResult::Payload(fsys::EventPayload::CapabilityRouted(payload)))
}

fn maybe_create_empty_payload(event_type: EventType) -> Option<fsys::EventResult> {
    let result = match event_type {
        EventType::Destroyed => {
            fsys::EventResult::Payload(fsys::EventPayload::Destroyed(fsys::DestroyedPayload {}))
        }
        EventType::Discovered => {
            fsys::EventResult::Payload(fsys::EventPayload::Discovered(fsys::DiscoveredPayload {}))
        }
        EventType::MarkedForDestruction => fsys::EventResult::Payload(
            fsys::EventPayload::MarkedForDestruction(fsys::MarkedForDestructionPayload {}),
        ),
        EventType::Resolved => {
            fsys::EventResult::Payload(fsys::EventPayload::Resolved(fsys::ResolvedPayload {}))
        }
        EventType::Started => {
            fsys::EventResult::Payload(fsys::EventPayload::Started(fsys::StartedPayload {}))
        }
        _ => fsys::EventResult::__UnknownVariant { ordinal: 999, bytes: vec![], handles: vec![] },
    };
    Some(result)
}

fn maybe_create_empty_error_payload(error: &EventError) -> Option<fsys::EventResult> {
    let error_payload = match error.event_type() {
        EventType::Destroyed => fsys::EventErrorPayload::Destroyed(fsys::DestroyedError {}),
        EventType::Discovered => fsys::EventErrorPayload::Discovered(fsys::DiscoveredError {}),
        EventType::MarkedForDestruction => {
            fsys::EventErrorPayload::MarkedForDestruction(fsys::MarkedForDestructionError {})
        }
        EventType::Resolved => fsys::EventErrorPayload::Resolved(fsys::ResolvedError {}),
        EventType::Started => fsys::EventErrorPayload::Started(fsys::StartedError {}),
        EventType::Stopped => fsys::EventErrorPayload::Stopped(fsys::StoppedError {}),
        _ => fsys::EventErrorPayload::__UnknownVariant {
            ordinal: 999,
            bytes: vec![],
            handles: vec![],
        },
    };
    Some(fsys::EventResult::Error(fsys::EventError {
        error_payload: Some(error_payload),
        description: Some(format!("{}", error.source)),
        ..fsys::EventError::empty()
    }))
}

/// Creates the basic FIDL Event object containing the event type, target_realm
/// and basic handler for resumption.
async fn create_event_fidl_object(event: Event) -> Result<fsys::Event, fidl::Error> {
    let event_type = Some(event.event.event_type().into());
    let timestamp = Some(event.event.timestamp.into_nanos());
    let target_relative_moniker =
        RelativeMoniker::from_absolute(&event.scope_moniker, &event.event.target_moniker);
    let descriptor = Some(fsys::ComponentDescriptor {
        moniker: Some(target_relative_moniker.to_string()),
        component_url: Some(event.event.component_url.clone()),
    });
    let event_result = maybe_create_event_result(&event.scope_moniker, &event.event.result).await?;
    let handler = maybe_serve_handler_async(event);
    Ok(fsys::Event { event_type, descriptor, handler, event_result, timestamp })
}

/// Serves the server end of the RoutingProtocol FIDL protocol asynchronously.
fn serve_routing_protocol_async(
    capability_provider: Arc<Mutex<Option<Box<dyn CapabilityProvider>>>>,
) -> ClientEnd<fsys::RoutingProtocolMarker> {
    let (client_end, stream) = create_request_stream::<fsys::RoutingProtocolMarker>()
        .expect("failed to create request stream for RoutingProtocol");
    fasync::Task::spawn(async move {
        serve_routing_protocol(capability_provider, stream).await;
    })
    .detach();
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
        flags: u32,
        open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = channel::take_channel(server_end);
        let path_str = relative_path.to_str().expect("Relative path must be valid unicode");
        self.proxy
            .open(server_end, flags, open_mode, path_str)
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
            fsys::RoutingProtocolRequest::ReplaceAndOpen {
                client_end,
                mut server_end,
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
                    if let Err(e) =
                        existing_provider.open(0, 0, PathBuf::new(), &mut server_end).await
                    {
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
    fasync::Task::spawn(async move {
        // Expect exactly one call to Resume
        let mut out_responder = None;
        if let Some(Ok(fsys::HandlerRequest::Resume { responder })) = stream.next().await {
            out_responder = Some(responder);
        }
        // Always resume the event even if the stream has closed.
        event.resume();
        if let Some(responder) = out_responder {
            if let Err(e) = responder.send() {
                debug!("failed to respond to Resume request: {:?}", e);
            }
        }
    })
    .detach();
    Some(client_end)
}
