// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    async_trait::async_trait,
    fidl::endpoints::{create_request_stream, ClientEnd, ServerEnd, ServiceMarker},
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client::{connect_channel_to_service, connect_to_service},
    fuchsia_zircon as zx,
    futures::StreamExt,
    std::{convert::TryFrom, sync::atomic::AtomicBool},
    thiserror::Error,
};

/// Returns the string name for the given `event_type`
pub fn event_name(event_type: &fsys::EventType) -> String {
    match event_type {
        fsys::EventType::CapabilityReady => "capability_ready",
        fsys::EventType::CapabilityRequested => "capability_requested",
        fsys::EventType::CapabilityRouted => "capability_routed",
        fsys::EventType::Destroyed => "destroyed",
        fsys::EventType::Discovered => "discovered",
        fsys::EventType::MarkedForDestruction => "",
        fsys::EventType::Resolved => "resolved",
        fsys::EventType::Started => "started",
        fsys::EventType::Stopped => "stopped",
        fsys::EventType::Running => "running",
    }
    .to_string()
}

/// A wrapper over the BlockingEventSource FIDL proxy.
/// Provides all of the FIDL methods with a cleaner, simpler interface.
/// Refer to events.fidl for a detailed description of this protocol.
pub struct EventSource {
    proxy: fsys::BlockingEventSourceProxy,
    running: AtomicBool,
}

impl EventSource {
    /// Connects to the BlockingEventSource service at its default location
    /// The default location is presumably "/svc/fuchsia.sys2.BlockingEventSource"
    pub fn new_sync() -> Result<Self, Error> {
        let proxy = connect_to_service::<fsys::BlockingEventSourceMarker>()
            .context("could not connect to BlockingEventSource service")?;
        Ok(EventSource::from_proxy(proxy))
    }

    pub fn new_async() -> Result<Self, Error> {
        let (proxy, server_end) =
            fidl::endpoints::create_proxy::<fsys::BlockingEventSourceMarker>()?;
        connect_channel_to_service::<fsys::EventSourceMarker>(server_end.into_channel())
            .context("could not connect to EventSource service")?;
        Ok(EventSource::from_proxy(proxy))
    }

    /// Wraps a provided BlockingEventSource proxy
    pub fn from_proxy(proxy: fsys::BlockingEventSourceProxy) -> Self {
        Self { proxy, running: AtomicBool::new(false) }
    }

    pub async fn subscribe(&self, event_names: Vec<impl AsRef<str>>) -> Result<EventStream, Error> {
        if self.running.load(std::sync::atomic::Ordering::SeqCst) {
            return Err(format_err!(
                "The component tree is already running! Subscribing to new events is racy!"
            ));
        }
        let (client_end, stream) = create_request_stream::<fsys::EventStreamMarker>()?;
        let subscription =
            self.proxy.subscribe(&mut event_names.iter().map(|e| e.as_ref()), client_end);

        subscription.await?.map_err(|error| format_err!("Error: {:?}", error))?;
        Ok(EventStream::new(stream))
    }

    pub async fn start_component_tree(&self) {
        let was_running =
            self.running.compare_and_swap(false, true, std::sync::atomic::Ordering::SeqCst);
        if !was_running {
            self.proxy.start_component_tree().await.unwrap();
        }
    }
}

pub struct EventStream {
    stream: fsys::EventStreamRequestStream,
}

#[derive(Debug, Error, Clone)]
pub enum EventStreamError {
    #[error("Stream terminated unexpectedly")]
    StreamClosed,
}

impl EventStream {
    pub fn new(stream: fsys::EventStreamRequestStream) -> Self {
        Self { stream }
    }

    pub async fn next(&mut self) -> Result<fsys::Event, EventStreamError> {
        match self.stream.next().await {
            Some(Ok(fsys::EventStreamRequest::OnEvent { event, .. })) => Ok(event),
            Some(_) => Err(EventStreamError::StreamClosed),
            None => Err(EventStreamError::StreamClosed),
        }
    }
}

/// Common features of any event - event type, target moniker, conversion function
pub trait Event: Handler + TryFrom<fsys::Event, Error = anyhow::Error> {
    const TYPE: fsys::EventType;
    const NAME: &'static str;

    fn target_moniker(&self) -> &str;
    fn component_url(&self) -> &str;
    fn timestamp(&self) -> zx::Time;
    fn is_ok(&self) -> bool;
    fn is_err(&self) -> bool;
}

/// Basic handler that resumes/unblocks from an Event
#[must_use = "invoke resume() otherwise component manager will be halted indefinitely!"]
#[async_trait]
pub trait Handler: Sized + Send {
    /// Returns a proxy to unblock the associated component manager task.
    /// If this is an asynchronous event, then this will return none.
    fn handler_proxy(self) -> Option<fsys::HandlerProxy>;

    #[must_use = "futures do nothing unless you await on them!"]
    async fn resume<'a>(self) -> Result<(), fidl::Error> {
        if let Some(proxy) = self.handler_proxy() {
            return proxy.resume().await;
        }
        Ok(())
    }
}

/// Implemented on fsys::Event for resuming a generic event
impl Handler for fsys::Event {
    fn handler_proxy(self) -> Option<fsys::HandlerProxy> {
        if let Some(handler) = self.handler {
            handler.into_proxy().ok()
        } else {
            None
        }
    }
}

/// A protocol that allows routing capabilities over FIDL.
#[async_trait]
pub trait RoutingProtocol {
    fn protocol_proxy(&self) -> Option<fsys::RoutingProtocolProxy>;

    #[must_use = "futures do nothing unless you await on them!"]
    async fn set_provider(
        &self,
        client_end: ClientEnd<fsys::CapabilityProviderMarker>,
    ) -> Result<(), fidl::Error> {
        if let Some(proxy) = self.protocol_proxy() {
            return proxy.set_provider(client_end).await;
        }
        Ok(())
    }
}

#[derive(Debug, PartialEq, Eq, Clone, Ord, PartialOrd)]
/// Simplifies the exit status represented by an Event. All stop status values
/// that indicate failure are crushed into `Crash`.
pub enum ExitStatus {
    Clean,
    Crash(i32),
}

impl From<i32> for ExitStatus {
    fn from(exit_status: i32) -> Self {
        match exit_status {
            0 => ExitStatus::Clean,
            _ => ExitStatus::Crash(exit_status),
        }
    }
}

#[derive(Debug)]
struct ComponentDescriptor {
    component_url: String,
    moniker: String,
}

impl TryFrom<fsys::ComponentDescriptor> for ComponentDescriptor {
    type Error = anyhow::Error;

    fn try_from(descriptor: fsys::ComponentDescriptor) -> Result<Self, Self::Error> {
        let component_url = descriptor.component_url.ok_or(format_err!("No component url"))?;
        let moniker = descriptor.moniker.ok_or(format_err!("No moniker"))?;
        Ok(ComponentDescriptor { component_url, moniker })
    }
}

#[derive(Debug, PartialEq, Eq)]
pub struct EventError {
    pub description: String,
}

/// The macro defined below will automatically create event classes corresponding
/// to their events.fidl and hooks.rs counterparts. Every event class implements
/// the Event and Handler traits. These minimum requirements allow every event to
/// be handled by the events client library.

/// Creates an event class based on event type and an optional payload
/// * event_type -> FIDL name for event type
/// * payload -> If an event has a payload, describe the additional params:
///   * name -> FIDL name for the payload
///   * data -> If a payload contains data items, describe the additional params:
///     * name -> FIDL name for the data item
///     * ty -> Rust type for the data item
///   * client_protocols -> If a payload contains client-side protocols, describe
///     the additional params:
///     * name -> FIDL name for the protocol
///     * ty -> Rust type for the protocol proxy
///   * server_protocols -> If a payload contains server-side protocols, describe
///     the additional params:
///     * name -> FIDL name for the protocol
// TODO(fxbug.dev/53937): This marco is getting complicated. Consider replacing it
//                  with a procedural macro.
macro_rules! create_event {
    // Entry points
    (
        event_type: $event_type:ident,
        event_name: $event_name:ident,
        payload: {
            data: {$(
                {
                    name: $data_name:ident,
                    ty: $data_ty:ty,
                }
            )*},
            client_protocols: {$(
                {
                    name: $client_protocol_name:ident,
                    ty: $client_protocol_ty:ty,
                }
            )*},
            server_protocols: {$(
                {
                    name: $server_protocol_name:ident,
                }
            )*},
        },
        error_payload: {
            $(
                {
                    name: $error_data_name:ident,
                    ty: $error_data_ty:ty,
                }
            )*
        }
    ) => {
        paste::paste! {
            pub struct [<$event_type Payload>] {
                $(pub $client_protocol_name: $client_protocol_ty,)*
                $(pub $server_protocol_name: Option<zx::Channel>,)*
                $(pub $data_name: $data_ty,)*
            }

            #[derive(Debug)]
            pub struct [<$event_type Error>] {
                $(pub $error_data_name: $error_data_ty,)*
                pub description: String,
            }

            pub struct $event_type {
                descriptor: ComponentDescriptor,
                handler: Option<fsys::HandlerProxy>,
                timestamp: zx::Time,
                pub result: Result<[<$event_type Payload>], [<$event_type Error>]>,
            }

            impl $event_type {
                pub fn unwrap_payload<'a>(&'a self) -> &'a [<$event_type Payload>] {
                    self.result.as_ref().unwrap()
                }

                $(
                    pub fn [<take_ $server_protocol_name>]<T: ServiceMarker>(&mut self)
                            -> Option<T::RequestStream> {
                        self.result.as_mut()
                            .ok()
                            .and_then(|payload| payload.$server_protocol_name.take())
                            .and_then(|channel| {
                                let server_end = ServerEnd::<T>::new(channel);
                                server_end.into_stream().ok()
                            })
                    }
                )*
            }

            impl Event for $event_type {
                const TYPE: fsys::EventType = fsys::EventType::$event_type;
                const NAME: &'static str = stringify!($event_name);

                fn target_moniker(&self) -> &str {
                    &self.descriptor.moniker
                }

                fn component_url(&self) -> &str {
                    &self.descriptor.component_url
                }

                fn timestamp(&self) -> zx::Time {
                    self.timestamp
                }

                fn is_ok(&self) -> bool {
                    self.result.is_ok()
                }

                fn is_err(&self) -> bool {
                    self.result.is_err()
                }
            }

            impl TryFrom<fsys::Event> for $event_type {
                type Error = anyhow::Error;

                fn try_from(event: fsys::Event) -> Result<Self, Self::Error> {
                    // Extract the payload from the Event object.
                    let result = match event.event_result {
                        Some(fsys::EventResult::Payload(payload)) => {
                            // This payload will be unused for event types that have no additional
                            // fields.
                            #[allow(unused)]
                            let payload = match payload {
                                fsys::EventPayload::$event_type(payload) => Ok(payload),
                                _ => Err(format_err!("Incorrect payload type")),
                            }?;

                            // Extract the additional data from the Payload object.
                            $(
                                let $data_name: $data_ty = payload.$data_name.ok_or(
                                    format_err!("Missing $data_name from $event_type object")
                                )?.into();
                            )*

                            // Extract the additional protocols from the Payload object.
                            $(
                                let $client_protocol_name: $client_protocol_ty = payload.$client_protocol_name.ok_or(
                                    format_err!("Missing $client_protocol_name from $event_type object")
                                )?.into_proxy()?;
                            )*
                            $(
                                let $server_protocol_name: Option<zx::Channel> =
                                    Some(payload.$server_protocol_name.ok_or(
                                        format_err!("Missing $server_protocol_name from $event_type object")
                                    )?);
                            )*

                            #[allow(dead_code)]
                            let payload = paste::paste! {
                                [<$event_type Payload>] {
                                    $($data_name,)*
                                    $($client_protocol_name,)*
                                    $($server_protocol_name,)*
                                }
                            };

                            Ok(Ok(payload))
                        },
                        Some(fsys::EventResult::Error(err)) => {
                            let description = err.description.ok_or(
                                format_err!("Missing error description")
                                )?;

                            let error_payload = err.error_payload.ok_or(
                                format_err!("Missing error_payload from EventError object")
                                )?;

                            // This error payload will be unused for event types that have no
                            // additional fields.
                            #[allow(unused)]
                            let err = match error_payload {
                                fsys::EventErrorPayload::$event_type(err) => Ok(err),
                                _ => Err(format_err!("Incorrect payload type")),
                            }?;

                            // Extract the additional data from the Payload object.
                            $(
                                let $error_data_name: $error_data_ty =
                                    err.$error_data_name.ok_or(
                                        format_err!("Missing $error_data_name from $error_payload_name object")
                                    )?;
                            )*

                            let error_payload =
                                [<$event_type Error>] { $($error_data_name,)* description };
                            Ok(Err(error_payload))
                        },
                        None => Err(format_err!("Missing event_result from Event object")),
                        _ => Err(format_err!("Unexpected event result")),
                    }?;

                    let event = {
                        // Event type in event must match what is expected
                        let event_type = event.event_type.ok_or(
                            format_err!("Missing event_type from Event object")
                        )?;

                        if event_type != Self::TYPE {
                            return Err(format_err!("Incorrect event type"));
                        }

                        let descriptor = event.descriptor
                            .ok_or(format_err!("Missing descriptor Event object"))
                            .and_then(|descriptor| ComponentDescriptor::try_from(descriptor))?;

                        let timestamp = zx::Time::from_nanos(event.timestamp.ok_or(
                            format_err!("Missing timestamp from the Event object")
                        )?);

                        let handler = event.handler.map(|h| h.into_proxy()).transpose()?;

                        $event_type { descriptor,  handler,  timestamp, result }
                    };
                    Ok(event)
                }
            }

            impl Handler for $event_type {
                fn handler_proxy(self) -> Option<fsys::HandlerProxy> {
                    self.handler
                }
            }
        }
    };
    ($event_type:ident, $event_name:ident) => {
        create_event!(event_type: $event_type, event_name: $event_name,
                      payload: {
                          data: {},
                          client_protocols: {},
                          server_protocols: {},
                      },
                      error_payload: {});
    };
}

// To create a class for an event, use the above macro here.
create_event!(Destroyed, destroyed);
create_event!(Discovered, discovered);
create_event!(MarkedForDestruction, marked_for_destruction);
create_event!(Resolved, resolved);
create_event!(Started, started);
create_event!(
    event_type: Stopped,
    event_name: stopped,
    payload: {
        data: {
            {
                name: status,
                ty: ExitStatus,
            }
        },
        client_protocols: {},
        server_protocols: {},
    },
    error_payload: {}
);
create_event!(
    event_type: Running,
    event_name: running,
    payload: {
        data: {
            {
                name: started_timestamp,
                ty: i64,
            }
        },
        client_protocols: {},
        server_protocols: {},
    },
    error_payload: {
        {
            name: started_timestamp,
            ty: i64,
        }
    }
);
create_event!(
    event_type: CapabilityReady,
    event_name: capability_ready,
    payload: {
        data: {
            {
                name: path,
                ty: String,
            }
        },
        client_protocols: {
            {
                name: node,
                ty: fio::NodeProxy,
            }
        },
        server_protocols: {},
    },
    error_payload: {
        {
            name: path,
            ty: String,
        }
    }
);
create_event!(
    event_type: CapabilityRequested,
    event_name: capability_requested,
    payload: {
        data: {
            {
                name: path,
                ty: String,
            }
        },
        client_protocols: {},
        server_protocols: {
            {
                name: capability,
            }
        },
    },
    error_payload: {
        {
            name: path,
            ty: String,
        }
    }
);
create_event!(
    event_type: CapabilityRouted,
    event_name: capability_routed,
    payload: {
        data: {
            {
                name: source,
                ty: fsys::CapabilitySource,
            }
            {
                name: capability_id,
                ty: String,
            }
        },
        client_protocols: {
            {
                name: routing_protocol,
                ty: fsys::RoutingProtocolProxy,
            }
        },
        server_protocols: {},
    },
    error_payload: {
        {
            name: capability_id,
            ty: String,
        }
    }
);

impl RoutingProtocol for CapabilityRouted {
    fn protocol_proxy(&self) -> Option<fsys::RoutingProtocolProxy> {
        self.result.as_ref().ok().map(|payload| payload.routing_protocol.clone())
    }
}
