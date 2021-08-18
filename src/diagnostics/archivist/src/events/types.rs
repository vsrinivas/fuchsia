// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        container::ComponentIdentity,
        events::error::{EventError, MonikerError},
    },
    async_trait::async_trait,
    fidl::endpoints::{ProtocolMarker, ServerEnd},
    fidl_fuchsia_inspect::TreeProxy,
    fidl_fuchsia_inspect_deprecated::InspectProxy,
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_logger::{LogSinkMarker, LogSinkRequestStream},
    fidl_fuchsia_sys2::{self as fsys, Event, EventHeader},
    fidl_fuchsia_sys_internal::SourceIdentity,
    fidl_table_validation::*,
    fuchsia_zircon as zx,
    futures::{channel::mpsc, stream::BoxStream},
    io_util,
    std::{convert::TryFrom, ops::Deref},
};

#[async_trait]
pub trait EventSource: Sync + Send {
    async fn listen(&mut self, sender: mpsc::Sender<ComponentEvent>) -> Result<(), EventError>;
}

/// The capacity for bounded channels used by this implementation.
pub static CHANNEL_CAPACITY: usize = 1024;

#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub struct Moniker(Vec<String>);

impl Deref for Moniker {
    type Target = Vec<String>;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl Into<Moniker> for Vec<&str> {
    fn into(self) -> Moniker {
        Moniker(self.into_iter().map(|s| s.to_string()).collect())
    }
}

impl Into<Moniker> for Vec<String> {
    fn into(self) -> Moniker {
        Moniker(self)
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct UniqueKey(Vec<String>);

impl Deref for UniqueKey {
    type Target = Vec<String>;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl Into<UniqueKey> for Vec<&str> {
    fn into(self) -> UniqueKey {
        UniqueKey(self.into_iter().map(|s| s.to_string()).collect())
    }
}

/// Represents the ID of a component.
#[derive(Debug, Clone, Eq, Hash, PartialEq)]
pub enum ComponentIdentifier {
    Legacy {
        /// The realm path plus the name of the component.
        moniker: Moniker,

        /// The instance ID of the component.
        instance_id: String,
    },
    Moniker(Vec<MonikerSegment>),
}

impl ComponentIdentifier {
    /// Returns the relative moniker to be used for selectors.
    /// For legacy components (v1), this is the relative moniker with respect to the root realm.
    pub fn relative_moniker_for_selectors(&self) -> Moniker {
        match self {
            Self::Legacy { moniker, .. } => moniker.clone(),
            Self::Moniker(segments) => {
                if segments.is_empty() {
                    Moniker(vec![])
                } else {
                    Moniker(segments.iter().map(|s| s.to_string()).collect())
                }
            }
        }
    }

    pub fn unique_key(&self) -> UniqueKey {
        match self {
            Self::Legacy { instance_id, .. } => {
                let mut key = self.relative_moniker_for_selectors().0;
                key.push(instance_id.clone());
                UniqueKey(key)
            }
            Self::Moniker(segments) => {
                let mut key = vec![];
                for segment in segments {
                    key.push(segment.to_string());
                    key.push(segment.instance_id.clone());
                }
                UniqueKey(key)
            }
        }
    }

    pub fn parse_from_moniker(moniker: &str) -> Result<Self, MonikerError> {
        if moniker == "<component_manager>" {
            return Ok(ComponentIdentifier::Moniker(vec![MonikerSegment {
                collection: None,
                name: "<component_manager>".to_string(),
                instance_id: "0".to_string(),
            }]));
        }

        if moniker == "." {
            return Ok(ComponentIdentifier::Moniker(vec![]));
        }

        let without_root = moniker
            .strip_prefix("./")
            .ok_or_else(|| MonikerError::InvalidMonikerPrefix(moniker.to_string()))?;

        let mut segments = vec![];
        for raw_segment in without_root.split("/") {
            let mut parts = raw_segment.split(":");
            let segment = match (parts.next(), parts.next(), parts.next()) {
                // we have a collection, a component name, and an instance id
                (Some(c), Some(n), Some(i)) => MonikerSegment {
                    collection: Some(c.to_string()),
                    name: n.to_string(),
                    instance_id: i.to_string(),
                },
                // we have a name and an instance id, no collection
                (Some(n), Some(i), None) => MonikerSegment {
                    collection: None,
                    name: n.to_string(),
                    instance_id: i.to_string(),
                },
                _ => return Err(MonikerError::InvalidSegment(raw_segment.to_string())),
            };
            segments.push(segment);
        }

        Ok(ComponentIdentifier::Moniker(segments))
    }
}

impl std::fmt::Display for ComponentIdentifier {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Legacy { moniker, instance_id } => {
                for (i, segment) in moniker.iter().enumerate() {
                    if i > 0 {
                        write!(f, "/")?;
                    }
                    write!(f, "{}", segment)?;
                }
                write!(f, ":{}", instance_id)
            }
            Self::Moniker(segments) => {
                if segments.is_empty() {
                    write!(f, ".")
                } else {
                    for (i, segment) in segments.iter().enumerate() {
                        if i > 0 {
                            write!(f, "/")?;
                        }
                        write!(f, "{}", segment)?;
                    }
                    Ok(())
                }
            }
        }
    }
}

/// A single segment in the moniker of a component.
#[derive(Debug, Clone, Eq, Hash, PartialEq)]
pub struct MonikerSegment {
    /// The name of the component's collection, if any.
    pub collection: Option<String>,
    /// The name of the component.
    pub name: String,
    /// The instance of the component.
    pub instance_id: String,
}

impl std::fmt::Display for MonikerSegment {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if let Some(collection) = &self.collection {
            write!(f, "{}:", collection)?;
        }
        write!(f, "{}", self.name)
    }
}

#[derive(Debug, Clone, ValidFidlTable, PartialEq)]
#[fidl_table_src(SourceIdentity)]
pub struct ValidatedSourceIdentity {
    pub realm_path: Vec<String>,
    pub component_url: String,
    pub component_name: String,
    pub instance_id: String,
}

#[derive(Debug, ValidFidlTable)]
#[fidl_table_src(EventHeader)]
pub struct ValidatedEventHeader {
    pub event_type: fsys::EventType,
    pub component_url: String,
    pub moniker: String,
    pub timestamp: i64,
}

#[derive(Debug, ValidFidlTable)]
#[fidl_table_src(Event)]
pub struct ValidatedEvent {
    /// Information about the component for which this event was generated.
    pub header: ValidatedEventHeader,

    /// Optional payload for some event types
    #[fidl_field_type(optional)]
    pub event_result: Option<fsys::EventResult>,
}

/// The ID of a component as used in components V1.
/// Represents the shared data associated with
/// all component events.
#[derive(Debug, PartialEq, Clone)]
pub struct EventMetadata {
    pub identity: ComponentIdentity,

    pub timestamp: zx::Time,
}

impl EventMetadata {
    pub fn new(identity: ComponentIdentity) -> Self {
        Self { identity, timestamp: zx::Time::get_monotonic() }
    }
}

impl TryFrom<SourceIdentity> for EventMetadata {
    type Error = EventError;
    fn try_from(component: SourceIdentity) -> Result<Self, Self::Error> {
        Ok(EventMetadata {
            identity: ComponentIdentity::try_from(component)?,
            timestamp: zx::Time::get_monotonic(),
        })
    }
}

/// Represents the diagnostics data associated
/// with a component being observed starting.
#[derive(Debug, PartialEq)]
pub struct StartEvent {
    pub metadata: EventMetadata,
}

/// Represents the diagnostics data associated
/// with a component being observed running.
#[derive(Debug, PartialEq)]
pub struct RunningEvent {
    pub metadata: EventMetadata,

    pub component_start_time: zx::Time,
}

/// Represents the diagnostics data associated
/// with a component being observed stopping.
#[derive(Debug, PartialEq)]
pub struct StopEvent {
    pub metadata: EventMetadata,
}

/// Represents the diagnostics data associated
/// with a new Diagnostics Directory being
/// made available.
#[derive(Debug)]
pub struct DiagnosticsReadyEvent {
    pub metadata: EventMetadata,

    /// Proxy to the inspect data host.
    pub directory: Option<DirectoryProxy>,
}

/// A new incoming connection to `LogSink`.
pub struct LogSinkRequestedEvent {
    pub metadata: EventMetadata,
    pub requests: LogSinkRequestStream,
}

impl LogSinkRequestedEvent {
    fn new(event: ValidatedEvent, metadata: EventMetadata) -> Result<Self, EventError> {
        let payload = event.event_result.ok_or(EventError::MissingField("event_result")).and_then(
            |result| match result {
                fsys::EventResult::Payload(fsys::EventPayload::CapabilityRequested(payload)) => {
                    Ok(payload)
                }
                fsys::EventResult::Error(fsys::EventError {
                    description: Some(description),
                    ..
                }) => Err(EventError::ReceivedError { description }),
                result => Err(EventError::UnrecognizedResult { result }),
            },
        )?;

        let capability_name = payload.name.ok_or(EventError::MissingField("name"))?;
        if &capability_name != LogSinkMarker::NAME {
            Err(EventError::IncorrectName {
                received: capability_name,
                expected: LogSinkMarker::NAME,
            })?;
        }

        let capability = payload.capability.ok_or(EventError::MissingField("capability"))?;
        let requests = ServerEnd::<LogSinkMarker>::new(capability)
            .into_stream()
            .map_err(|source| EventError::InvalidServerEnd { source })?;

        Ok(Self { metadata, requests })
    }
}

impl std::fmt::Debug for LogSinkRequestedEvent {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("LogSinkRequestedEvent").field("metadata", &self.metadata).finish()
    }
}

pub type ComponentEventChannel = mpsc::Sender<ComponentEvent>;

/// A stream of |ComponentEvent|s
pub type ComponentEventStream = BoxStream<'static, ComponentEvent>;

/// An event that occurred to a component.
#[derive(Debug)]
pub enum ComponentEvent {
    /// We observed the component starting.
    Start(StartEvent),

    /// We observed the component already started.
    Running(RunningEvent),

    /// We observed the component stopping.
    Stop(StopEvent),

    /// We observed the creation of a new `out` directory.
    DiagnosticsReady(DiagnosticsReadyEvent),

    /// We received a new connection to `LogSink`.
    LogSinkRequested(LogSinkRequestedEvent),
}

/// Data associated with a component.
/// This data is stored by data collectors and passed by the collectors to processors.
#[derive(Debug)]
pub enum InspectData {
    /// Empty data, for testing.
    Empty,

    /// A VMO containing data associated with the event.
    Vmo(zx::Vmo),

    /// A file containing data associated with the event.
    ///
    /// Because we can't synchronously retrieve file contents like we can for VMOs, this holds
    /// the full file contents. Future changes should make streaming ingestion feasible.
    File(Vec<u8>),

    /// A connection to a Tree service and a handle to the root hierarchy VMO. This VMO is what a
    /// root.inspect file would contain and the result of calling Tree#GetContent. We hold to it
    /// so that we can use it when the component is removed, at which point calling the Tree
    /// service is not an option.
    Tree(TreeProxy, Option<zx::Vmo>),

    /// A connection to the deprecated Inspect service.
    DeprecatedFidl(InspectProxy),
}

impl TryFrom<Event> for ComponentEvent {
    type Error = EventError;

    fn try_from(event: Event) -> Result<ComponentEvent, Self::Error> {
        let event: ValidatedEvent = ValidatedEvent::try_from(event)?;

        let metadata = EventMetadata {
            identity: ComponentIdentity::from_identifier_and_url(
                &ComponentIdentifier::parse_from_moniker(&event.header.moniker)?,
                &event.header.component_url,
            ),
            timestamp: zx::Time::from_nanos(event.header.timestamp),
        };

        match event.header.event_type {
            fsys::EventType::Started => {
                let start_event = StartEvent { metadata };
                Ok(ComponentEvent::Start(start_event))
            }
            fsys::EventType::Stopped => {
                let stop_event = StopEvent { metadata };
                Ok(ComponentEvent::Stop(stop_event))
            }
            fsys::EventType::DirectoryReady | fsys::EventType::Running => {
                construct_payload_holding_component_event(event, metadata)
            }
            fsys::EventType::CapabilityRequested => {
                Ok(ComponentEvent::LogSinkRequested(LogSinkRequestedEvent::new(event, metadata)?))
            }
            _ => Err(EventError::InvalidEventType { ty: event.header.event_type }),
        }
    }
}

fn construct_payload_holding_component_event(
    event: ValidatedEvent,
    shared_data: EventMetadata,
) -> Result<ComponentEvent, EventError> {
    match event.event_result {
        Some(result) => {
            match result {
                fsys::EventResult::Payload(fsys::EventPayload::DirectoryReady(directory_ready)) => {
                    let name = directory_ready.name.ok_or(EventError::MissingField("name"))?;
                    if name == "diagnostics" {
                        match directory_ready.node {
                            Some(node) => {
                                let diagnostics_ready_event = DiagnosticsReadyEvent {
                                    metadata: shared_data,
                                    directory: io_util::node_to_directory(node.into_proxy()?).ok(),
                                };
                                Ok(ComponentEvent::DiagnosticsReady(diagnostics_ready_event))
                            }
                            None => Err(EventError::MissingDiagnosticsDir),
                        }
                    } else {
                        Err(EventError::IncorrectName { received: name, expected: "diagnostics" })
                    }
                }
                fsys::EventResult::Payload(fsys::EventPayload::Running(payload)) => {
                    match payload.started_timestamp {
                        Some(timestamp) => {
                            let existing_data = RunningEvent {
                                metadata: shared_data,
                                component_start_time: zx::Time::from_nanos(timestamp),
                            };
                            Ok(ComponentEvent::Running(existing_data))
                        }
                        None => Err(EventError::MissingStartTimestamp),
                    }
                }
                fsys::EventResult::Error(fsys::EventError {
                    description: Some(description),
                    ..
                }) => {
                    // TODO(fxbug.dev/53903): result.error carries information about errors that happened
                    // in component_manager. We should dump those in diagnostics.
                    Err(EventError::ReceivedError { description })
                }
                result => Err(EventError::UnrecognizedResult { result }),
            }
        }
        None => Err(EventError::MissingPayload { event }),
    }
}

impl PartialEq for ComponentEvent {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (ComponentEvent::Start(a), ComponentEvent::Start(b)) => {
                return a == b;
            }
            (ComponentEvent::Stop(a), ComponentEvent::Stop(b)) => {
                return a == b;
            }
            (ComponentEvent::DiagnosticsReady(a), ComponentEvent::DiagnosticsReady(b)) => {
                return a == b;
            }
            (ComponentEvent::Running(a), ComponentEvent::Running(b)) => {
                return a == b;
            }
            // we can't check two LogSinkRequested events for equality because they have channels
            _ => false,
        }
    }
}

// Requires a custom partial_eq due to the presence of a directory proxy.
// Two events with the same metadata and different directory proxies
// will be considered the same.
impl PartialEq for DiagnosticsReadyEvent {
    fn eq(&self, other: &Self) -> bool {
        self.metadata == other.metadata
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::logs::testing::create_log_sink_requested_event;
    use std::convert::TryInto;

    #[test]
    fn convert_v2_moniker_for_diagnostics() {
        let identifier = ComponentIdentifier::parse_from_moniker("./a:0").unwrap();
        assert_eq!(identifier.relative_moniker_for_selectors(), vec!["a"].into());
        assert_eq!(identifier.unique_key(), vec!["a", "0"].into());

        let identifier = ComponentIdentifier::parse_from_moniker("./a:0/b:1").unwrap();
        assert_eq!(identifier.relative_moniker_for_selectors(), vec!["a", "b"].into());
        assert_eq!(identifier.unique_key(), vec!["a", "0", "b", "1"].into());

        let identifier = ComponentIdentifier::parse_from_moniker("./a:0/coll:comp:1/b:0").unwrap();
        assert_eq!(identifier.relative_moniker_for_selectors(), vec!["a", "coll:comp", "b"].into());
        assert_eq!(identifier.unique_key(), vec!["a", "0", "coll:comp", "1", "b", "0"].into());

        let identifier = ComponentIdentifier::parse_from_moniker(".").unwrap();
        assert!(identifier.relative_moniker_for_selectors().is_empty());
        assert!(identifier.unique_key().is_empty());
    }

    #[fuchsia::test] // we need an executor for the fidl types
    async fn validate_logsink_requested_event() {
        let target_moniker = "./foo:0";
        let target_url = "http://foo.com".to_string();
        let (_log_sink_proxy, log_sink_server_end) =
            fidl::endpoints::create_proxy::<LogSinkMarker>().unwrap();
        let raw_event = create_log_sink_requested_event(
            target_moniker.to_owned(),
            target_url.clone(),
            log_sink_server_end.into_channel(),
        );
        let event = match raw_event.try_into().unwrap() {
            ComponentEvent::LogSinkRequested(e) => e,
            other => panic!("incorrect event type received: {:?}", other),
        };

        assert_eq!(
            event.metadata.identity,
            ComponentIdentity::from_identifier_and_url(
                &ComponentIdentifier::parse_from_moniker(target_moniker).unwrap(),
                target_url
            )
        );
    }
}
