// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, OptionalTask},
        channel,
        model::{
            error::ModelError,
            events::{
                event::Event, registry::EventSubscription, source::EventSource, stream::EventStream,
            },
            hooks::{
                EventError, EventErrorPayload, EventPayload, EventResult, EventType, HasEventType,
            },
        },
    },
    async_trait::async_trait,
    cm_rust::{CapabilityName, EventMode},
    fidl::endpoints::{create_request_stream, ClientEnd, Proxy},
    fidl_fuchsia_component as fcomponent,
    fidl_fuchsia_io::{self as fio, NodeProxy},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_trace as trace,
    fuchsia_zircon as zx,
    futures::{
        future::BoxFuture, lock::Mutex, select, stream::FuturesUnordered, FutureExt, StreamExt,
        TryStreamExt,
    },
    log::{debug, error, info, warn},
    moniker::{AbsoluteMoniker, ExtendedMoniker, RelativeMoniker},
    std::{path::PathBuf, sync::Arc},
};

pub async fn serve_event_source_sync(
    event_source: EventSource,
    stream: fsys::EventSourceRequestStream,
) {
    let result = stream
        .try_for_each_concurrent(None, move |request| {
            let mut event_source = event_source.clone();
            async move {
                match request {
                    fsys::EventSourceRequest::Subscribe { events, stream, responder } => {
                        // Subscribe to events.
                        let requests = events
                            .into_iter()
                            .filter(|request| {
                                request.event_name.is_some() && request.mode.is_some()
                            })
                            .map(|request| EventSubscription {
                                event_name: request
                                    .event_name
                                    .map(|name| CapabilityName::from(name))
                                    .unwrap(),
                                mode: match request.mode {
                                    Some(fsys::EventMode::Sync) => EventMode::Sync,
                                    _ => EventMode::Async,
                                },
                            })
                            .collect();

                        match event_source.subscribe(requests).await {
                            Ok(event_stream) => {
                                // Unblock the component
                                responder.send(&mut Ok(()))?;

                                // Serve the event_stream over FIDL asynchronously
                                serve_event_stream(event_stream, stream).await;
                            }
                            Err(e) => {
                                info!("Error subscribing to events: {:?}", e);
                                responder.send(&mut Err(fcomponent::Error::ResourceUnavailable))?;
                            }
                        };
                    }
                    fsys::EventSourceRequest::TakeStaticEventStream { path, responder, .. } => {
                        let mut result = event_source
                            .take_static_event_stream(path)
                            .await
                            .ok_or(fcomponent::Error::ResourceUnavailable);
                        responder.send(&mut result)?;
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
pub async fn serve_event_stream(
    mut event_stream: EventStream,
    client_end: ClientEnd<fsys::EventStreamMarker>,
) {
    let listener = client_end.into_proxy().expect("cannot create proxy from client_end");
    // Track sync event handlers here so they're automatically dropped if this event stream is dropped.
    let mut handlers = FuturesUnordered::new();

    loop {
        trace::duration!("component_manager", "events:fidl_get_next");
        // Poll both the event stream and the handlers until both futures complete.
        select! {
            maybe_event = event_stream.next().fuse() => {
                match maybe_event {
                    Some(event) => {
                        // Create the basic Event FIDL object.
                        // This will begin serving the Handler protocol asynchronously.
                        let (opt_fut, event_fidl_object) = match create_event_fidl_object(event).await {
                            Err(e) => {
                                warn!("Failed to create event object: {:?}", e);
                                continue;
                            }
                            Ok(res) => res,
                        };
                        if let Some(fut) = opt_fut {
                            handlers.push(fut);
                        }
                        if let Err(e) = listener.on_event(event_fidl_object) {
                            // It's not an error for the client to drop the listener.
                            if !e.is_closed() {
                                warn!("Unexpected error while serving EventStream: {:?}", e);
                            }
                            break;
                        }
                    },
                    None => {
                        break;
                    },
                }
            },
            _ = handlers.select_next_some() => {},
        }
    }
}

async fn maybe_create_event_result(
    scope: &ExtendedMoniker,
    event_result: &EventResult,
) -> Result<Option<fsys::EventResult>, fidl::Error> {
    match event_result {
        Ok(EventPayload::CapabilityReady { name, node, .. }) => {
            Ok(Some(create_capability_ready_payload(name.to_string(), node)?))
        }
        Ok(EventPayload::CapabilityRequested { name, capability, .. }) => Ok(Some(
            create_capability_requested_payload(name.to_string(), capability.clone()).await,
        )),
        Ok(EventPayload::CapabilityRouted { source, capability_provider, .. }) => {
            let scope_moniker = match scope {
                ExtendedMoniker::ComponentInstance(moniker) => moniker,
                _ => unreachable!(),
            };
            Ok(maybe_create_capability_routed_payload(
                scope_moniker,
                source,
                capability_provider.clone(),
            ))
        }
        Ok(EventPayload::Running { started_timestamp }) => Ok(Some(fsys::EventResult::Payload(
            fsys::EventPayload::Running(fsys::RunningPayload {
                started_timestamp: Some(started_timestamp.into_nanos()),
                ..fsys::RunningPayload::EMPTY
            }),
        ))),
        Ok(EventPayload::Stopped { status }) => Ok(Some(fsys::EventResult::Payload(
            fsys::EventPayload::Stopped(fsys::StoppedPayload {
                status: Some(status.into_raw()),
                ..fsys::StoppedPayload::EMPTY
            }),
        ))),
        Ok(payload) => Ok(maybe_create_empty_payload(payload.event_type())),
        Err(EventError {
            source,
            event_error_payload: EventErrorPayload::CapabilityReady { name },
        }) => Ok(Some(fsys::EventResult::Error(fsys::EventError {
            error_payload: Some(fsys::EventErrorPayload::CapabilityReady(
                fsys::CapabilityReadyError {
                    name: Some(name.to_string()),
                    ..fsys::CapabilityReadyError::EMPTY
                },
            )),
            description: Some(format!("{}", source)),
            ..fsys::EventError::EMPTY
        }))),
        Err(EventError {
            source,
            event_error_payload: EventErrorPayload::CapabilityRequested { name, .. },
        }) => Ok(Some(fsys::EventResult::Error(fsys::EventError {
            error_payload: Some(fsys::EventErrorPayload::CapabilityRequested(
                fsys::CapabilityRequestedError {
                    name: Some(name.to_string()),
                    ..fsys::CapabilityRequestedError::EMPTY
                },
            )),
            description: Some(format!("{}", source)),
            ..fsys::EventError::EMPTY
        }))),
        Err(EventError {
            source,
            event_error_payload: EventErrorPayload::Running { started_timestamp },
        }) => Ok(Some(fsys::EventResult::Error(fsys::EventError {
            error_payload: Some(fsys::EventErrorPayload::Running(fsys::RunningError {
                started_timestamp: Some(started_timestamp.into_nanos()),
                ..fsys::RunningError::EMPTY
            })),
            description: Some(format!("{}", source)),
            ..fsys::EventError::EMPTY
        }))),
        Err(error) => Ok(maybe_create_empty_error_payload(error)),
    }
}

fn create_capability_ready_payload(
    name: String,
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

    let payload = fsys::CapabilityReadyPayload {
        name: Some(name),
        node,
        ..fsys::CapabilityReadyPayload::EMPTY
    };
    Ok(fsys::EventResult::Payload(fsys::EventPayload::CapabilityReady(payload)))
}

async fn create_capability_requested_payload(
    name: String,
    capability: Arc<Mutex<Option<zx::Channel>>>,
) -> fsys::EventResult {
    let capability = capability.lock().await.take();
    match capability {
        Some(capability) => {
            let payload = fsys::CapabilityRequestedPayload {
                name: Some(name),
                capability: Some(capability),
                ..fsys::CapabilityRequestedPayload::EMPTY
            };
            fsys::EventResult::Payload(fsys::EventPayload::CapabilityRequested(payload))
        }
        None => {
            // This can happen if a hook takes the capability prior to the events system.
            let payload =
                fsys::EventErrorPayload::CapabilityRequested(fsys::CapabilityRequestedError {
                    name: Some(name),
                    ..fsys::CapabilityRequestedError::EMPTY
                });
            fsys::EventResult::Error(fsys::EventError {
                error_payload: Some(payload),
                description: Some("Capability unavailable".to_string()),
                ..fsys::EventError::EMPTY
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

    let name = source.source_name().map(|n| n.to_string());
    let source = Some(match source {
        CapabilitySource::Component { component, .. } => {
            let component = component.upgrade().ok()?;
            let source_moniker = RelativeMoniker::from_absolute(scope, &component.abs_moniker);
            fsys::CapabilitySource::Component(fsys::ComponentCapability {
                source_moniker: Some(source_moniker.to_string()),
                ..fsys::ComponentCapability::EMPTY
            })
        }
        CapabilitySource::Capability { component, .. } => {
            let component = component.upgrade().ok()?;
            let source_moniker = RelativeMoniker::from_absolute(scope, &component.abs_moniker);
            fsys::CapabilitySource::Framework(fsys::FrameworkCapability {
                scope_moniker: Some(source_moniker.to_string()),
                ..fsys::FrameworkCapability::EMPTY
            })
        }
        CapabilitySource::Framework { component, .. } => {
            let scope_moniker = RelativeMoniker::from_absolute(scope, &component.moniker);
            fsys::CapabilitySource::Framework(fsys::FrameworkCapability {
                scope_moniker: Some(scope_moniker.to_string()),
                ..fsys::FrameworkCapability::EMPTY
            })
        }
        CapabilitySource::Builtin { .. } | CapabilitySource::Namespace { .. } => {
            fsys::CapabilitySource::AboveRoot(fsys::AboveRootCapability {
                ..fsys::AboveRootCapability::EMPTY
            })
        }
    });

    let payload = fsys::CapabilityRoutedPayload {
        routing_protocol,
        name,
        source,
        ..fsys::CapabilityRoutedPayload::EMPTY
    };
    Some(fsys::EventResult::Payload(fsys::EventPayload::CapabilityRouted(payload)))
}

fn maybe_create_empty_payload(event_type: EventType) -> Option<fsys::EventResult> {
    let result = match event_type {
        EventType::Destroyed => {
            fsys::EventResult::Payload(fsys::EventPayload::Destroyed(fsys::DestroyedPayload::EMPTY))
        }
        EventType::Discovered => fsys::EventResult::Payload(fsys::EventPayload::Discovered(
            fsys::DiscoveredPayload::EMPTY,
        )),
        EventType::MarkedForDestruction => fsys::EventResult::Payload(
            fsys::EventPayload::MarkedForDestruction(fsys::MarkedForDestructionPayload::EMPTY),
        ),
        EventType::Resolved => {
            fsys::EventResult::Payload(fsys::EventPayload::Resolved(fsys::ResolvedPayload::EMPTY))
        }
        EventType::Started => {
            fsys::EventResult::Payload(fsys::EventPayload::Started(fsys::StartedPayload::EMPTY))
        }
        _ => fsys::EventResult::unknown(999, Default::default()),
    };
    Some(result)
}

fn maybe_create_empty_error_payload(error: &EventError) -> Option<fsys::EventResult> {
    let error_payload = match error.event_type() {
        EventType::Destroyed => fsys::EventErrorPayload::Destroyed(fsys::DestroyedError::EMPTY),
        EventType::Discovered => fsys::EventErrorPayload::Discovered(fsys::DiscoveredError::EMPTY),
        EventType::MarkedForDestruction => {
            fsys::EventErrorPayload::MarkedForDestruction(fsys::MarkedForDestructionError::EMPTY)
        }
        EventType::Resolved => fsys::EventErrorPayload::Resolved(fsys::ResolvedError::EMPTY),
        EventType::Started => fsys::EventErrorPayload::Started(fsys::StartedError::EMPTY),
        EventType::Stopped => fsys::EventErrorPayload::Stopped(fsys::StoppedError::EMPTY),
        _ => fsys::EventErrorPayload::unknown(999, Default::default()),
    };
    Some(fsys::EventResult::Error(fsys::EventError {
        error_payload: Some(error_payload),
        description: Some(format!("{}", error.source)),
        ..fsys::EventError::EMPTY
    }))
}

/// Creates the basic FIDL Event object containing the event type, target_moniker
/// and basic handler for resumption. It returns a tuple of the future to run the
/// handler, and the FIDL event.
async fn create_event_fidl_object(
    event: Event,
) -> Result<(Option<BoxFuture<'static, ()>>, fsys::Event), fidl::Error> {
    let moniker_string = match (&event.event.target_moniker, &event.scope_moniker) {
        (moniker @ ExtendedMoniker::ComponentManager, _) => moniker.to_string(),
        (ExtendedMoniker::ComponentInstance(target), ExtendedMoniker::ComponentManager) => {
            RelativeMoniker::from_absolute(&AbsoluteMoniker::root(), &target).to_string()
        }
        (ExtendedMoniker::ComponentInstance(target), ExtendedMoniker::ComponentInstance(scope)) => {
            RelativeMoniker::from_absolute(&scope, &target).to_string()
        }
    };
    let header = Some(fsys::EventHeader {
        event_type: Some(event.event.event_type().into()),
        moniker: Some(moniker_string),
        component_url: Some(event.event.component_url.clone()),
        timestamp: Some(event.event.timestamp.into_nanos()),
        ..fsys::EventHeader::EMPTY
    });
    let event_result = maybe_create_event_result(&event.scope_moniker, &event.event.result).await?;
    let (opt_fut, handler) = maybe_serve_handler_async(event);
    Ok((opt_fut, fsys::Event { header, handler, event_result, ..fsys::Event::EMPTY }))
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
    ) -> Result<OptionalTask, ModelError> {
        let server_end = channel::take_channel(server_end);
        let path_str = relative_path.to_str().expect("Relative path must be valid unicode");
        self.proxy
            .open(server_end, flags, open_mode, path_str)
            .await
            .expect("failed to invoke CapabilityProvider::Open over FIDL");
        Ok(None.into())
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
fn maybe_serve_handler_async(
    event: Event,
) -> (Option<BoxFuture<'static, ()>>, Option<ClientEnd<fsys::HandlerMarker>>) {
    if event.mode() == EventMode::Async {
        return (None, None);
    }
    let (client_end, mut stream) = create_request_stream::<fsys::HandlerMarker>()
        .expect("could not create request stream for handler protocol");
    let handler_fut = async move {
        // Expect exactly one call to Resume
        let mut out_responder = None;
        if let Ok(Some(fsys::HandlerRequest::Resume { responder })) = stream.try_next().await {
            out_responder = Some(responder);
        }
        // Always resume the event even if the stream has closed.
        event.resume();
        if let Some(responder) = out_responder {
            if let Err(e) = responder.send() {
                debug!("failed to respond to Resume request: {:?}", e);
            }
        }
    }
    .boxed();
    (Some(handler_fut), Some(client_end))
}
