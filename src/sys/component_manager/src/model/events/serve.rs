// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
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
    cm_rust::{CapabilityName, EventMode},
    cm_util::io::clone_dir,
    fidl::endpoints::{ClientEnd, Proxy, ServerEnd},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fsys::EventStream2RequestStream,
    fuchsia_zircon::{
        self as zx, sys::ZX_CHANNEL_MAX_MSG_BYTES, sys::ZX_CHANNEL_MAX_MSG_HANDLES, HandleBased,
    },
    futures::{lock::Mutex, StreamExt, TryStreamExt},
    measure_tape_for_events::Measurable,
    moniker::{
        AbsoluteMoniker, AbsoluteMonikerBase, ChildMoniker, ChildMonikerBase, ExtendedMoniker,
        RelativeMoniker, RelativeMonikerBase,
    },
    std::sync::Arc,
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
    // Length is the length of the scope (0 if no scope, 1 for the
    // component after <root>)
    let mut length = 0;
    // Index is the current index in route +1 (since we want to exclude
    // a component's parent from the moniker).
    let mut index = 1;
    for component in route {
        // Set length to index, this is the most recent
        // scope found in the route.
        if component.scope.is_some() {
            length = index;
        }
        index += 1;
    }
    length
}

/// Determines if an event from a specified absolute moniker
/// may be routed to a given scope.
fn is_moniker_valid_within_scope(moniker: &ExtendedMoniker, route: &[ComponentEventRoute]) -> bool {
    match moniker {
        ExtendedMoniker::ComponentInstance(instance) => {
            validate_component_instance(instance, route.iter())
        }
        ExtendedMoniker::ComponentManager => false,
    }
}

fn get_child_moniker_name(moniker: &ChildMoniker) -> &str {
    moniker.collection().unwrap_or(moniker.name())
}

/// Checks the specified instance against the specified route,
/// Returns Some(true) if the route is explicitly allowed,
/// Some(false) if the route is explicitly rejected,
/// or None if allowed because no route explicitly rejected it.
fn validate_component_instance(
    instance: &AbsoluteMoniker,
    mut iter: std::slice::Iter<'_, ComponentEventRoute>,
) -> bool {
    let path = instance.path();
    let mut event_iter = path.iter();
    // Component manager is an unnamed component which exists in route
    // but not in the moniker (because it's not a named component).
    // We take the first item from the iterator and get its scope
    // to determine initial scoping and ensure that route
    // and moniker are properly aligned to each other.
    let mut active_scope = iter.next().unwrap().scope.clone();
    for component in iter {
        if let Some(event_part) = event_iter.next() {
            match active_scope {
                Some(ref scopes)
                    if !scopes.contains(&get_child_moniker_name(&event_part).to_string()) =>
                {
                    // Reject due to scope mismatch
                    return false;
                }
                _ => {}
            }
            if event_part.name() != component.component {
                // Reject due to path mismatch
                return false;
            }
            active_scope = component.scope.clone();
        } else {
            // Reject due to no more event parts
            return false;
        }
    }
    match (active_scope, event_iter.next()) {
        (Some(scopes), Some(event)) => {
            if !scopes.contains(&get_child_moniker_name(&event).to_string()) {
                // Reject due to scope mismatch
                return false;
            }
        }
        (Some(_), None) => {
            // Reject due to no more event parts
            return false;
        }
        _ => {}
    }
    // Reached end of scope.
    true
}

/// Filters and downscopes an event by a route.
/// Returns true if the event is allowed given the specified route,
/// false otherwise.
fn filter_event(moniker: &mut ExtendedMoniker, route: &[ComponentEventRoute]) -> bool {
    let scope_length = get_scope_length(route);
    if !is_moniker_valid_within_scope(&moniker, &route[0..scope_length]) {
        return false;
    }
    // For scoped events, the apparent root (visible to the component)
    // starts at the last scope declaration which applies to this particular event.
    // Since this creates a relative rather than absolute moniker, where the base may be different
    // for each event, ambiguous component monikers are possible here.
    if let ExtendedMoniker::ComponentInstance(instance) = moniker {
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
pub fn validate_and_filter_event(
    moniker: &mut ExtendedMoniker,
    route: &[ComponentEventRoute],
) -> bool {
    let needs_filter = route.iter().any(|component| component.scope.is_some());
    if needs_filter {
        filter_event(moniker, route)
    } else {
        true
    }
}

async fn handle_get_next_request(
    event_stream: &mut EventStream,
    pending_event: &mut Option<fsys::Event>,
) -> Option<Vec<fsys::Event>> {
    // Handle buffered state
    // TODO(https://fxbug.dev/98653): Replace this
    // with a function to measure a Vec<fsys::Event>
    let mut bytes_used: usize = FIDL_HEADER_BYTES + FIDL_VECTOR_HEADER_BYTES;
    let mut handles_used: usize = 0;
    let mut events = vec![];

    /// Measures the size of an event, increments bytes used,
    /// and returns the event Vec if full.
    /// If there isn't enough space for even one event, logs an error
    /// and returns an empty Vec.
    macro_rules! handle_event {
        ($e: expr, $event_type: expr) => {
            let measure_tape = $e.measure();
            bytes_used += measure_tape.num_bytes;
            handles_used += measure_tape.num_handles;
            if bytes_used > ZX_CHANNEL_MAX_MSG_BYTES as usize
            || handles_used > ZX_CHANNEL_MAX_MSG_HANDLES as usize {
                if pending_event.is_some() {
                    unreachable!("Overflowed twice");
                }
                *pending_event = Some($e);
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

    macro_rules! handle_and_filter_event {
        ($event: expr, $route: expr) => {
            if let Some(mut route) = $route {
                route.reverse();
                if !validate_and_filter_event(&mut $event.event.target_moniker, &route) {
                    continue;
                }
            }
            let event_type = $event.event.event_type().to_string();
            let event_fidl_object = match create_event_fidl_object($event).await {
                Ok(event_fidl_object) => event_fidl_object,
                Err(error) => {
                    warn!(?error, "Failed to create event object");
                    continue;
                }
            };
            handle_event!(event_fidl_object, event_type);
        };
    }

    // Read overflowed events from the buffer first
    if let Some(event) = pending_event.take() {
        let e_type = event
            .header
            .as_ref()
            .map(|header| format!("{:?}", header.event_type))
            .unwrap_or("unknown".to_string());
        handle_event!(event, e_type);
    }

    if events.is_empty() {
        // Block
        // If not for the macro this would be an if let
        // because the loop will only iterate once (therefore we block only 1 time)
        while let Some((mut event, route)) = event_stream.next().await {
            handle_and_filter_event!(event, route);
            break;
        }
    }
    loop {
        if let Some(Some((mut event, route))) = event_stream.next_or_none().await {
            handle_and_filter_event!(event, route);
        } else {
            break;
        }
    }
    if events.is_empty() {
        None
    } else {
        Some(events)
    }
}

/// Tries to handle the next request.
/// An error value indicates the caller should close the channel
async fn try_handle_get_next_request(
    event_stream: &mut EventStream,
    responder: fsys::EventStream2GetNextResponder,
    buffer: &mut Option<fsys::Event>,
) -> bool {
    let events = handle_get_next_request(event_stream, buffer).await;
    if let Some(events) = events {
        responder.send(&mut events.into_iter()).is_ok()
    } else {
        unreachable!("Internal: The event_stream internal channel should never be closed.");
    }
}

/// Serves the event_stream_v2 protocol implemented for EventStream2RequestStream
/// This is needed because we get the request stream directly as a stream from FDIO
/// but as a ServerEnd from the hooks system.
pub async fn serve_event_stream_v2_as_stream(
    mut event_stream: EventStream,
    mut stream: EventStream2RequestStream,
) {
    let mut buffer = None;
    while let Some(Ok(request)) = stream.next().await {
        match request {
            fsys::EventStream2Request::GetNext { responder } => {
                if !try_handle_get_next_request(&mut event_stream, responder, &mut buffer).await {
                    // Close the channel if an error occurs while handling the request.
                    return;
                }
            }
            fsys::EventStream2Request::WaitForReady { responder } => {
                let _ = responder.send();
            }
        }
    }
}

/// Serves EventStream FIDL requests received over the provided stream.
pub async fn serve_event_stream_v2(
    event_stream: EventStream,
    server_end: ServerEnd<fsys::EventStream2Marker>,
) {
    let stream = server_end.into_stream().unwrap();
    serve_event_stream_v2_as_stream(event_stream, stream).await;
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
        Ok(EventPayload::CapabilityRouted { .. }) => {
            // Capability routed events cannot be exposed externally. This should be unreachable.
            Ok(None)
        }
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
async fn create_event_fidl_object(event: Event) -> Result<fsys::Event, anyhow::Error> {
    let moniker_string = match (&event.event.target_moniker, &event.scope_moniker) {
        (moniker @ ExtendedMoniker::ComponentManager, _) => moniker.to_string(),
        (ExtendedMoniker::ComponentInstance(target), ExtendedMoniker::ComponentManager) => {
            RelativeMoniker::scope_down(&AbsoluteMoniker::root(), target)
                .expect("every component can be scoped down from the root")
                .to_string()
        }
        (ExtendedMoniker::ComponentInstance(target), ExtendedMoniker::ComponentInstance(scope)) => {
            RelativeMoniker::scope_down(scope, target)
                .expect("target must be a child of event scope")
                .to_string()
        }
    };
    let header = Some(fsys::EventHeader {
        event_type: Some(event.event.event_type().try_into()?),
        moniker: Some(moniker_string),
        component_url: Some(event.event.component_url.clone()),
        timestamp: Some(event.event.timestamp.into_nanos()),
        ..fsys::EventHeader::EMPTY
    });
    let event_result = maybe_create_event_result(&event.event.result).await?;
    Ok(fsys::Event { header, event_result, ..fsys::Event::EMPTY })
}

#[cfg(test)]
mod tests {
    use crate::model::events::serve::validate_and_filter_event;
    use crate::model::events::serve::ComponentEventRoute;
    use moniker::AbsoluteMoniker;
    use moniker::AbsoluteMonikerBase;
    use moniker::ChildMoniker;
    use moniker::ExtendedMoniker;

    // Route: /root(coll)
    // Event: /root
    // Output: (rejected)
    #[test]
    fn test_validate_and_filter_event_at_root() {
        let mut moniker =
            ExtendedMoniker::ComponentInstance(AbsoluteMoniker::new(vec![ChildMoniker::try_new(
                "root",
                Some("coll"),
            )
            .unwrap()]));
        let route = vec![
            ComponentEventRoute { component: "<root>".to_string(), scope: None },
            ComponentEventRoute {
                component: "root".to_string(),
                scope: Some(vec!["coll".to_string()]),
            },
        ];
        assert!(!validate_and_filter_event(&mut moniker, &route));
    }

    // Test validate_and_filter_event

    // Route: /<root>/core(test_manager)/test_manager
    // Event: /
    // Output: (rejected)
    #[test]
    fn test_validate_and_filter_event_empty_moniker() {
        let mut event = ExtendedMoniker::ComponentInstance(AbsoluteMoniker::new(vec![]));
        let route = vec![
            ComponentEventRoute { component: "<root>".to_string(), scope: None },
            ComponentEventRoute {
                component: "core".to_string(),
                scope: Some(vec!["test_manager".to_string()]),
            },
            ComponentEventRoute { component: "test_manager".to_string(), scope: None },
        ];
        assert_eq!(validate_and_filter_event(&mut event, &route), false);
    }

    // Route: a(b)/b(c)/c
    // Event: a/b/c
    // Output: /
    #[test]
    fn test_validate_and_filter_event_moniker_root() {
        let mut event = ExtendedMoniker::ComponentInstance(AbsoluteMoniker::new(vec![
            ChildMoniker::try_new("a", None).unwrap(),
            ChildMoniker::try_new("b", None).unwrap(),
            ChildMoniker::try_new("c", None).unwrap(),
        ]));
        let route = vec![
            ComponentEventRoute { component: "<root>".to_string(), scope: None },
            ComponentEventRoute { component: "a".to_string(), scope: Some(vec!["b".to_string()]) },
            ComponentEventRoute { component: "b".to_string(), scope: Some(vec!["c".to_string()]) },
            ComponentEventRoute { component: "c".to_string(), scope: None },
        ];
        assert!(super::validate_and_filter_event(&mut event, &route));
        assert_eq!(event, ExtendedMoniker::ComponentInstance(AbsoluteMoniker::new(vec![])));
    }

    // Route: a(b)/b(c)/c
    // Event: a/b/c/d
    // Output: d
    #[test]
    fn test_validate_and_filter_event_moniker_children_scoped() {
        let mut event = ExtendedMoniker::ComponentInstance(AbsoluteMoniker::new(vec![
            ChildMoniker::try_new("a", None).unwrap(),
            ChildMoniker::try_new("b", None).unwrap(),
            ChildMoniker::try_new("c", None).unwrap(),
            ChildMoniker::try_new("d", None).unwrap(),
        ]));
        let route = vec![
            ComponentEventRoute { component: "<root>".to_string(), scope: None },
            ComponentEventRoute { component: "a".to_string(), scope: Some(vec!["b".to_string()]) },
            ComponentEventRoute { component: "b".to_string(), scope: Some(vec!["c".to_string()]) },
            ComponentEventRoute { component: "c".to_string(), scope: None },
        ];
        assert!(super::validate_and_filter_event(&mut event, &route));
        assert_eq!(
            event,
            ExtendedMoniker::ComponentInstance(AbsoluteMoniker::new(vec![ChildMoniker::try_new(
                "d", None
            )
            .unwrap(),]))
        );
    }

    // Route: a(b)/b(c)/c
    // Event: a
    // Output: (rejected)
    #[test]
    fn test_validate_and_filter_event_moniker_above_root_rejected() {
        let mut event =
            ExtendedMoniker::ComponentInstance(AbsoluteMoniker::new(vec![ChildMoniker::try_new(
                "a", None,
            )
            .unwrap()]));
        let route = vec![
            ComponentEventRoute { component: "<root>".to_string(), scope: None },
            ComponentEventRoute { component: "a".to_string(), scope: Some(vec!["b".to_string()]) },
            ComponentEventRoute { component: "b".to_string(), scope: Some(vec!["c".to_string()]) },
            ComponentEventRoute { component: "c".to_string(), scope: None },
        ];
        assert!(!super::validate_and_filter_event(&mut event, &route));
        assert_eq!(
            event,
            ExtendedMoniker::ComponentInstance(AbsoluteMoniker::new(vec![ChildMoniker::try_new(
                "a", None
            )
            .unwrap(),]))
        );
    }

    // Route: a/b(c)/c
    // Event: f/i
    // Output: (rejected)
    #[test]
    fn test_validate_and_filter_event_moniker_ambiguous() {
        let mut event = ExtendedMoniker::ComponentInstance(AbsoluteMoniker::new(vec![
            ChildMoniker::try_new("f", None).unwrap(),
            ChildMoniker::try_new("i", None).unwrap(),
        ]));
        let route = vec![
            ComponentEventRoute { component: "<root>".to_string(), scope: None },
            ComponentEventRoute { component: "a".to_string(), scope: None },
            ComponentEventRoute { component: "b".to_string(), scope: Some(vec!["c".to_string()]) },
            ComponentEventRoute { component: "c".to_string(), scope: None },
        ];
        assert!(!super::validate_and_filter_event(&mut event, &route));
    }

    // Route: /core(test_manager)/test_manager/test-id(test_wrapper)/test_wrapper(test_root)
    // Event: /core/feedback
    // Output: (rejected)
    #[test]
    fn test_validate_and_filter_event_moniker_root_rejected() {
        let mut event = ExtendedMoniker::ComponentInstance(AbsoluteMoniker::new(vec![
            ChildMoniker::try_new("core", None).unwrap(),
            ChildMoniker::try_new("feedback", None).unwrap(),
        ]));
        let route = vec![
            ComponentEventRoute { component: "<root>".to_string(), scope: None },
            ComponentEventRoute {
                component: "core".to_string(),
                scope: Some(vec!["test_manager".to_string()]),
            },
            ComponentEventRoute {
                component: "test_manager".to_string(),
                scope: Some(vec!["test_wrapper".to_string()]),
            },
            ComponentEventRoute {
                component: "test_wrapper".to_string(),
                scope: Some(vec!["test_root".to_string()]),
            },
        ];
        assert_eq!(super::validate_and_filter_event(&mut event, &route), false);
    }
}
