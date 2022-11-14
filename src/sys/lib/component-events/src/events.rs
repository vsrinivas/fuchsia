// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::{ProtocolMarker, ServerEnd},
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_zircon as zx,
    lazy_static::lazy_static,
    std::{collections::VecDeque, convert::TryFrom},
    thiserror::Error,
};

lazy_static! {
    /// The path of the static event stream that, by convention, synchronously listens for
    /// Resolved events.
    pub static ref START_COMPONENT_TREE_STREAM: String = "StartComponentTree".into();
}

/// Returns the string name for the given `event_type`
pub fn event_name(event_type: &fsys::EventType) -> String {
    match event_type {
        fsys::EventType::CapabilityRequested => "capability_requested",
        fsys::EventType::DirectoryReady => "directory_ready",
        fsys::EventType::Discovered => "discovered",
        fsys::EventType::Destroyed => "destroyed",
        fsys::EventType::Resolved => "resolved",
        fsys::EventType::Unresolved => "unresolved",
        fsys::EventType::Started => "started",
        fsys::EventType::Stopped => "stopped",
        fsys::EventType::DebugStarted => "debug_started",
    }
    .to_string()
}

enum InternalStream {
    New(fsys::EventStream2Proxy),
}

pub struct EventStream {
    stream: InternalStream,
    buffer: VecDeque<fsys::Event>,
}

#[derive(Debug, Error, Clone)]
pub enum EventStreamError {
    #[error("Stream terminated unexpectedly")]
    StreamClosed,
}

impl EventStream {
    pub fn new_v2(stream: fsys::EventStream2Proxy) -> Self {
        Self { stream: InternalStream::New(stream), buffer: VecDeque::new() }
    }

    pub fn open_at_path_pipelined(path: impl Into<String>) -> Result<Self, Error> {
        Ok(Self::new_v2(connect_to_protocol_at_path::<fsys::EventStream2Marker>(&path.into())?))
    }

    pub async fn open_at_path(path: impl Into<String>) -> Result<Self, Error> {
        let event_stream = connect_to_protocol_at_path::<fsys::EventStream2Marker>(&path.into())?;
        event_stream.wait_for_ready().await?;
        Ok(Self::new_v2(event_stream))
    }

    pub async fn open() -> Result<Self, Error> {
        let event_stream = connect_to_protocol_at_path::<fsys::EventStream2Marker>(
            "/svc/fuchsia.component.EventStream",
        )?;
        event_stream.wait_for_ready().await?;
        Ok(Self::new_v2(event_stream))
    }

    pub fn open_pipelined() -> Result<Self, Error> {
        Ok(Self::new_v2(connect_to_protocol_at_path::<fsys::EventStream2Marker>(
            "/svc/fuchsia.component.EventStream",
        )?))
    }

    pub async fn next(&mut self) -> Result<fsys::Event, EventStreamError> {
        if let Some(event) = self.buffer.pop_front() {
            return Ok(event);
        }
        match &mut self.stream {
            InternalStream::New(stream) => {
                match stream.get_next().await {
                    Ok(events) => {
                        let mut iter = events.into_iter();
                        if let Some(real_event) = iter.next() {
                            let ret = real_event;
                            while let Some(value) = iter.next() {
                                self.buffer.push_back(value);
                            }
                            return Ok(ret);
                        } else {
                            // This should never happen, we should always
                            // have at least one event.
                            Err(EventStreamError::StreamClosed)
                        }
                    }
                    Err(_) => Err(EventStreamError::StreamClosed),
                }
            }
        }
    }
}

/// Common features of any event - event type, target moniker, conversion function
pub trait Event: TryFrom<fsys::Event, Error = anyhow::Error> {
    const TYPE: fsys::EventType;
    const NAME: &'static str;

    fn target_moniker(&self) -> &str;
    fn component_url(&self) -> &str;
    fn timestamp(&self) -> zx::Time;
    fn is_ok(&self) -> bool;
    fn is_err(&self) -> bool;
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
struct EventHeader {
    event_type: fsys::EventType,
    component_url: String,
    moniker: String,
    timestamp: zx::Time,
}

impl TryFrom<fsys::EventHeader> for EventHeader {
    type Error = anyhow::Error;

    fn try_from(header: fsys::EventHeader) -> Result<Self, Self::Error> {
        let event_type = header.event_type.ok_or(format_err!("No event type"))?;
        let component_url = header.component_url.ok_or(format_err!("No component url"))?;
        let moniker = header.moniker.ok_or(format_err!("No moniker"))?;
        let timestamp = zx::Time::from_nanos(
            header.timestamp.ok_or(format_err!("Missing timestamp from the Event object"))?,
        );
        Ok(EventHeader { event_type, component_url, moniker, timestamp })
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
            #[derive(Debug)]
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

            #[derive(Debug)]
            pub struct $event_type {
                header: EventHeader,
                result: Result<[<$event_type Payload>], [<$event_type Error>]>,
            }

            impl $event_type {
                pub fn result<'a>(&'a self) -> Result<&'a [<$event_type Payload>], &'a [<$event_type Error>]> {
                    self.result.as_ref()
                }

                $(
                    pub fn [<take_ $server_protocol_name>]<T: ProtocolMarker>(&mut self)
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
                    &self.header.moniker
                }

                fn component_url(&self) -> &str {
                    &self.header.component_url
                }

                fn timestamp(&self) -> zx::Time {
                    self.header.timestamp
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
                                _ => Err(format_err!("Incorrect payload type, {:?}", payload)),
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
                        let header = event.header
                            .ok_or(format_err!("Missing Event header"))
                            .and_then(|header| EventHeader::try_from(header))?;

                        if header.event_type != Self::TYPE {
                            return Err(format_err!("Incorrect event type"));
                        }

                        $event_type { header, result }
                    };
                    Ok(event)
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
create_event!(Discovered, discovered);
create_event!(Destroyed, destroyed);
create_event!(Resolved, resolved);
create_event!(Unresolved, unresolved);
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
    event_type: DirectoryReady,
    event_name: directory_ready,
    payload: {
        data: {
            {
                name: name,
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
            name: name,
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
                name: name,
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
            name: name,
            ty: String,
        }
    }
);
create_event!(
    event_type: DebugStarted,
    event_name: debug_started,
    payload: {
        data: {
            {
                name: break_on_start,
                ty: zx::EventPair,
            }
        },
        client_protocols: {
            {
                name: runtime_dir,
                ty: fio::DirectoryProxy,
            }
        },
        server_protocols: {},
    },
    error_payload: {}
);
