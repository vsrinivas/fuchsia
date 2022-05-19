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
    cm_moniker::InstancedExtendedMoniker,
    cm_rust::{CapabilityName, EventMode},
    cm_util::io::clone_dir,
    fidl::endpoints::{ClientEnd, Proxy},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon::{self as zx, HandleBased},
    futures::{lock::Mutex, TryStreamExt},
    log::{error, info, warn},
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, RelativeMoniker, RelativeMonikerBase},
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

    while let Some(event) = event_stream.next().await {
        // Create the basic Event FIDL object.
        let event_fidl_object = match create_event_fidl_object(event).await {
            Err(e) => {
                warn!("Failed to create event object: {:?}", e);
                continue;
            }
            Ok(res) => res,
        };
        if let Err(e) = listener.on_event(event_fidl_object) {
            // It's not an error for the client to drop the listener.
            if !e.is_closed() {
                warn!("Unexpected error while serving EventStream: {:?}", e);
            }
            return;
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
        EventType::Purged => fsys::EventErrorPayload::Purged(fsys::PurgedError::EMPTY),
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
        (moniker @ InstancedExtendedMoniker::ComponentManager, _) => moniker.to_string(),
        (
            InstancedExtendedMoniker::ComponentInstance(target),
            InstancedExtendedMoniker::ComponentManager,
        ) => {
            RelativeMoniker::from_absolute(&AbsoluteMoniker::root(), &target.without_instance_ids())
                .to_string()
        }
        (
            InstancedExtendedMoniker::ComponentInstance(target),
            InstancedExtendedMoniker::ComponentInstance(scope),
        ) => RelativeMoniker::from_absolute::<AbsoluteMoniker>(
            &scope.without_instance_ids(),
            &target.without_instance_ids(),
        )
        .to_string(),
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
