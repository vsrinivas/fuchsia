// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::CapabilitySource,
        model::{
            events::{
                event::Event,
                registry::{ComponentEventRoute, EventSubscription},
                source::EventSource,
                stream::EventStream,
            },
            hooks::{
                EventError, EventErrorPayload, EventPayload, EventResult, EventType, HasEventType,
            },
        },
    },
    cm_rust::{CapabilityName, EventMode},
    cm_util::io::clone_dir,
    fidl::endpoints::{ClientEnd, Proxy, ServerEnd},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon::{self as zx, sys::ZX_CHANNEL_MAX_MSG_BYTES, HandleBased},
    futures::{lock::Mutex, FutureExt, StreamExt, TryStreamExt},
    measure_tape_for_events::Measurable,
    moniker::{
        AbsoluteMoniker, AbsoluteMonikerBase, ChildMonikerBase, ExtendedMoniker, RelativeMoniker,
        RelativeMonikerBase,
    },
    std::{collections::VecDeque, sync::Arc},
    tracing::{error, info, warn},
};

// Number of bytes the header of a vector occupies in a fidl message.
// TODO(https://fxbug.dev/98653): This should be a constant in a FIDL library.
const FIDL_VECTOR_HEADER_BYTES: usize = 16;

// Number of bytes the header of a fidl message occupies.
// TODO(https://fxbug.dev/98653): This should be a constant in a FIDL library.
const FIDL_HEADER_BYTES: usize = 16;

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
                            .filter(|request| request.event_name.is_some())
                            .map(|request| EventSubscription {
                                event_name: request
                                    .event_name
                                    .map(|name| CapabilityName::from(name))
                                    .unwrap(),
                                mode: EventMode::Async,
                            })
                            .collect();

                        match event_source.subscribe(requests).await {
                            Ok(event_stream) => {
                                // Unblock the component
                                responder.send(&mut Ok(()))?;

                                // Serve the event_stream over FIDL asynchronously
                                serve_event_stream(event_stream, stream).await;
                            }
                            Err(error) => {
                                info!(?error, "Couldn't subscribe to events");
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
    if let Err(error) = result {
        error!(%error, "Couldn't serve EventSource");
    }
}

/// Serves EventStream FIDL requests received over the provided stream.
pub async fn serve_event_stream(
    mut event_stream: EventStream,
    client_end: ClientEnd<fsys::EventStreamMarker>,
) {
    let listener = client_end.into_proxy().expect("cannot create proxy from client_end");

    while let Some((event, _)) = event_stream.next().await {
        // Create the basic Event FIDL object.
        let event_fidl_object = match create_event_fidl_object(event).await {
            Err(error) => {
                warn!(?error, "Failed to create event object");
                continue;
            }
            Ok(res) => res,
        };
        if let Err(error) = listener.on_event(event_fidl_object) {
            // It's not an error for the client to drop the listener.
            if !error.is_closed() {
                warn!(?error, "Unexpected error while serving EventStream");
            }
            return;
        }
    }
}

/// Computes the scope length, which is the number of segments
/// up until the first scope. This is used to re-map the moniker
/// of an event before passing it off to the component that is
/// reading the event stream.
fn get_scope_length(route: &[ComponentEventRoute]) -> usize {
    // This is the wrong index, because it's operated on the unreversed vec.
    route.len()
        - route
            .iter()
            .enumerate()
            .find(|(_, route)| route.scope.is_some())
            .map(|(i, _)| i)
            .unwrap_or(0)
}

/// Determines if an event from a specified absolute moniker
/// may be routed to a given scope.
fn is_moniker_valid_within_scope(moniker: &ExtendedMoniker, route: &[ComponentEventRoute]) -> bool {
    match moniker {
        ExtendedMoniker::ComponentInstance(instance) => {
            let mut iter = route.iter().rev();
            match iter.next() {
                None => panic!("Event stream exited prematurely"),
                Some(active_scope) => {
                    validate_component_instance(instance, iter, active_scope.scope.clone())
                        .unwrap_or(true) // If no rejection for a route, allow it.
                }
            }
        }
        ExtendedMoniker::ComponentManager => false,
    }
}

/// Checks the specified instance against the specified route,
/// Returns Some(true) if the route is explicitly allowed,
/// Some(false) if the route is explicitly rejected,
/// or None if allowed because no route explicitly rejected it.
fn validate_component_instance(
    instance: &AbsoluteMoniker,
    iter: std::iter::Rev<std::slice::Iter<'_, ComponentEventRoute>>,
    mut active_scope: Option<Vec<String>>,
) -> Option<bool> {
    let mut reject_by_route = false;
    instance.path().iter().zip(iter.map(Some).chain(std::iter::repeat(None))).any(
        |(part, maybe_route)| {
            match (maybe_route, &active_scope) {
                (Some(_), Some(scope)) if !scope.contains(&part.name().to_string()) => {
                    // Reject due to scope mismatch.
                    reject_by_route = true;
                    true
                }
                (Some(route), Some(_)) => {
                    active_scope = route.scope.clone();
                    false
                }
                _ => {
                    // Permission granted, hit end of route
                    active_scope = None;
                    true
                }
            }
        },
    );
    if reject_by_route || active_scope.is_some() {
        return Some(false);
    }
    None
}

/// Filters and downscopes an event by a route.
/// Returns true if the event is allowed given the specified route,
/// false otherwise.
fn filter_event(event: &mut Event, route: &[ComponentEventRoute]) -> bool {
    let scope_length = get_scope_length(route);
    if !is_moniker_valid_within_scope(&event.event.target_moniker, route) {
        return false;
    }
    // For scoped events, the apparent root (visible to the component)
    // starts at the last scope declaration which applies to this particular event.
    // Since this creates a relative rather than absolute moniker, where the base may be different
    // for each event, ambiguous component monikers are possible here.
    if let ExtendedMoniker::ComponentInstance(instance) = &mut event.event.target_moniker {
        let mut path = instance.path().clone();
        path.reverse();
        for _ in 0..scope_length {
            path.pop();
        }
        path.reverse();
        *instance = AbsoluteMoniker::new(path);
    }
    true
}

/// Validates and filters an event, returning true if the route is allowed,
/// false otherwise. The scope of the event is filtered to the allowed route.
fn validate_and_filter_event(event: &mut Event, route: &[ComponentEventRoute]) -> bool {
    let needs_filter = route.iter().any(|component| component.scope.is_some());
    if needs_filter {
        filter_event(event, route)
    } else {
        true
    }
}

async fn handle_get_next_request(
    event_stream: &mut EventStream,
    buffer: &mut VecDeque<fsys::Event>,
) -> Option<Vec<fsys::Event>> {
    // Handle buffered state
    // TODO(https://fxbug.dev/98653): Replace this
    // with a function to measure a Vec<fsys::Event>
    let mut bytes_used: usize = FIDL_HEADER_BYTES + FIDL_VECTOR_HEADER_BYTES;
    let mut events = vec![];

    /// Creates a FIDL object from an event, logs
    /// an error and continues the loop on failure.
    macro_rules! create_fidl_object_or_continue {
        ($e: expr) => {
            match create_event_fidl_object($e).await {
                Ok(event_fidl_object) => event_fidl_object,
                Err(error) => {
                    warn!(?error, "Failed to create event object");
                    continue;
                }
            }
        };
    }

    /// Measures the size of an event, increments bytes used,
    /// and returns the event Vec if full.
    /// If there isn't enough space for even one event, logs an error
    /// and returns an empty Vec.
    macro_rules! handle_event {
        ($e: expr, $event_type: expr) => {
            bytes_used += $e.measure().num_bytes;
            if bytes_used > ZX_CHANNEL_MAX_MSG_BYTES as usize {
                buffer.push_back($e);
                if events.len() == 0 {
                    error!(
                        event_type = $event_type.as_str(),
                        "Event exceeded the maximum channel size, dropping event"
                    );
                }
                return Some(events);
            } else {
                events.push($e);
            }
        }
    }

    // Read overflowed events from the buffer first
    while let Some(event) = buffer.pop_front() {
        let e_type = event
            .header
            .as_ref()
            .map(|header| format!("{:?}", header.event_type))
            .unwrap_or("unknown".to_string());
        handle_event!(event, e_type);
    }

    // Read events from the event stream
    while let Some((mut event, Some(route))) = event_stream.next().await {
        if !validate_and_filter_event(&mut event, &route) {
            continue;
        }
        let event_type = event.event.event_type().to_string();
        let event_fidl_object = create_fidl_object_or_continue!(event);
        handle_event!(event_fidl_object, event_type);
        while let Some(Some((mut event, Some(route)))) = event_stream.next().now_or_never() {
            if !validate_and_filter_event(&mut event, &route) {
                // Event failed verification, check for next event
                continue;
            }
            let event_type = event.event.event_type().to_string();
            let event_fidl_object = create_fidl_object_or_continue!(event);
            handle_event!(event_fidl_object, event_type);
        }
        return Some(events);
    }
    None
}

/// Tries to handle the next request.
/// An error value indicates the caller should close the channel
async fn try_handle_get_next_request(
    event_stream: &mut EventStream,
    responder: fsys::EventStream2GetNextResponder,
    buffer: &mut VecDeque<fsys::Event>,
) -> bool {
    let events = handle_get_next_request(event_stream, buffer).await;
    if let Some(events) = events {
        responder.send(&mut events.into_iter()).is_ok()
    } else {
        unreachable!("Internal: The event_stream internal channel should never be closed.");
    }
}

/// Serves EventStream FIDL requests received over the provided stream.
pub async fn serve_event_stream_v2(
    mut event_stream: EventStream,
    server_end: ServerEnd<fsys::EventStream2Marker>,
) {
    let mut buffer = VecDeque::new();
    let mut stream = server_end.into_stream().unwrap();
    while let Some(Ok(request)) = stream.next().await {
        match request {
            fsys::EventStream2Request::GetNext { responder } => {
                if !try_handle_get_next_request(&mut event_stream, responder, &mut buffer).await {
                    // Close the channel if an error occurs while handling the request.
                    return;
                }
            }
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
        Ok(EventPayload::DebugStarted { runtime_dir, break_on_start }) => {
            Ok(Some(create_debug_started_payload(runtime_dir, break_on_start)))
        }
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
    node: &fio::NodeProxy,
) -> Result<fsys::EventResult, fidl::Error> {
    let node = {
        let (node_clone, server_end) = fidl::endpoints::create_proxy()?;
        node.clone(fio::OpenFlags::CLONE_SAME_RIGHTS, server_end)?;
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

fn create_debug_started_payload(
    runtime_dir: &Option<fio::DirectoryProxy>,
    break_on_start: &Arc<zx::EventPair>,
) -> fsys::EventResult {
    fsys::EventResult::Payload(fsys::EventPayload::DebugStarted(fsys::DebugStartedPayload {
        runtime_dir: clone_dir(runtime_dir.as_ref()).map(|dir| {
            dir.into_channel()
                .expect("could not convert directory to channel")
                .into_zx_channel()
                .into()
        }),
        break_on_start: break_on_start.duplicate_handle(zx::Rights::SAME_RIGHTS).ok(),
        ..fsys::DebugStartedPayload::EMPTY
    }))
}

fn maybe_create_empty_payload(event_type: EventType) -> Option<fsys::EventResult> {
    let result = match event_type {
        EventType::Discovered => fsys::EventResult::Payload(fsys::EventPayload::Discovered(
            fsys::DiscoveredPayload::EMPTY,
        )),
        EventType::Destroyed => {
            fsys::EventResult::Payload(fsys::EventPayload::Destroyed(fsys::DestroyedPayload::EMPTY))
        }
        EventType::Resolved => {
            fsys::EventResult::Payload(fsys::EventPayload::Resolved(fsys::ResolvedPayload::EMPTY))
        }
        EventType::Unresolved => fsys::EventResult::Payload(fsys::EventPayload::Unresolved(
            fsys::UnresolvedPayload::EMPTY,
        )),
        EventType::Started => {
            fsys::EventResult::Payload(fsys::EventPayload::Started(fsys::StartedPayload::EMPTY))
        }
        _ => fsys::EventResult::unknown(999, Default::default()),
    };
    Some(result)
}

fn maybe_create_empty_error_payload(error: &EventError) -> Option<fsys::EventResult> {
    let error_payload = match error.event_type() {
        EventType::Discovered => fsys::EventErrorPayload::Discovered(fsys::DiscoveredError::EMPTY),
        EventType::Destroyed => fsys::EventErrorPayload::Destroyed(fsys::DestroyedError::EMPTY),
        EventType::Resolved => fsys::EventErrorPayload::Resolved(fsys::ResolvedError::EMPTY),
        EventType::Unresolved => fsys::EventErrorPayload::Unresolved(fsys::UnresolvedError::EMPTY),
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

/// Creates the basic FIDL Event object
async fn create_event_fidl_object(event: Event) -> Result<fsys::Event, fidl::Error> {
    let moniker_string = match (&event.event.target_moniker, &event.scope_moniker) {
        (moniker @ ExtendedMoniker::ComponentManager, _) => moniker.to_string(),
        (ExtendedMoniker::ComponentInstance(target), ExtendedMoniker::ComponentManager) => {
            RelativeMoniker::from_absolute(&AbsoluteMoniker::root(), target).to_string()
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
    Ok(fsys::Event { header, event_result, ..fsys::Event::EMPTY })
}
