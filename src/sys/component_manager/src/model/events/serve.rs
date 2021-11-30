// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::CapabilitySource,
        model::{
            events::{
                event::Event, registry::EventSubscription, source::EventSource, stream::EventStream,
            },
            hooks::{
                EventError, EventErrorPayload, EventPayload, EventResult, EventType, HasEventType,
            },
        },
    },
    cm_rust::{CapabilityName, EventMode},
    fidl::endpoints::{create_request_stream, ClientEnd, Proxy},
    fidl_fuchsia_component as fcomponent,
    fidl_fuchsia_io::{self as fio, NodeProxy},
    fidl_fuchsia_sys2 as fsys, fuchsia_trace as trace, fuchsia_zircon as zx,
    futures::{
        future::BoxFuture, lock::Mutex, select, stream::FuturesUnordered, FutureExt, StreamExt,
        TryStreamExt,
    },
    log::{debug, error, info, warn},
    moniker::{
        AbsoluteMoniker, AbsoluteMonikerBase, ExtendedMoniker, RelativeMoniker, RelativeMonikerBase,
    },
    std::sync::Arc,
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
    event_result: &EventResult,
) -> Result<Option<fsys::EventResult>, fidl::Error> {
    match event_result {
        Ok(EventPayload::DirectoryReady { name, node, .. }) => {
            Ok(Some(create_directory_ready_payload(name.to_string(), node)?))
        }
        Ok(EventPayload::CapabilityRequested { name, capability, .. }) => Ok(Some(
            create_capability_requested_payload(name.to_string(), capability.clone()).await,
        )),
        Ok(EventPayload::CapabilityRouted { source, .. }) => {
            Ok(Some(create_capability_routed_payload(source)))
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
            event_error_payload: EventErrorPayload::DirectoryReady { name },
        }) => Ok(Some(fsys::EventResult::Error(fsys::EventError {
            error_payload: Some(fsys::EventErrorPayload::DirectoryReady(
                fsys::DirectoryReadyError {
                    name: Some(name.to_string()),
                    ..fsys::DirectoryReadyError::EMPTY
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

fn create_directory_ready_payload(
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

    let payload = fsys::DirectoryReadyPayload {
        name: Some(name),
        node,
        ..fsys::DirectoryReadyPayload::EMPTY
    };
    Ok(fsys::EventResult::Payload(fsys::EventPayload::DirectoryReady(payload)))
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

fn create_capability_routed_payload(source: &CapabilitySource) -> fsys::EventResult {
    let name = source.source_name().map(|n| n.to_string());
    let payload = fsys::CapabilityRoutedPayload { name, ..fsys::CapabilityRoutedPayload::EMPTY };
    fsys::EventResult::Payload(fsys::EventPayload::CapabilityRouted(payload))
}

fn maybe_create_empty_payload(event_type: EventType) -> Option<fsys::EventResult> {
    let result = match event_type {
        EventType::Purged => {
            fsys::EventResult::Payload(fsys::EventPayload::Purged(fsys::PurgedPayload::EMPTY))
        }
        EventType::Discovered => fsys::EventResult::Payload(fsys::EventPayload::Discovered(
            fsys::DiscoveredPayload::EMPTY,
        )),
        EventType::Destroyed => {
            fsys::EventResult::Payload(fsys::EventPayload::Destroyed(fsys::DestroyedPayload::EMPTY))
        }
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
        EventType::Purged => fsys::EventErrorPayload::Purged(fsys::PurgedError::EMPTY),
        EventType::Discovered => fsys::EventErrorPayload::Discovered(fsys::DiscoveredError::EMPTY),
        EventType::Destroyed => fsys::EventErrorPayload::Destroyed(fsys::DestroyedError::EMPTY),
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
            RelativeMoniker::from_absolute::<AbsoluteMoniker>(&scope, &target).to_string()
        }
    };
    let header = Some(fsys::EventHeader {
        event_type: Some(event.event.event_type().into()),
        moniker: Some(moniker_string),
        component_url: Some(event.event.component_url.clone()),
        timestamp: Some(event.event.timestamp.into_nanos()),
        ..fsys::EventHeader::EMPTY
    });
    let event_result = maybe_create_event_result(&event.event.result).await?;
    let (opt_fut, handler) = maybe_serve_handler_async(event);
    Ok((opt_fut, fsys::Event { header, handler, event_result, ..fsys::Event::EMPTY }))
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
