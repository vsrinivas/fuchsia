// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    events::error::{EventError, MonikerError},
    identity::ComponentIdentity,
};
use fidl::endpoints::ServerEnd;
use fidl::prelude::*;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_logger as flogger;
use fidl_fuchsia_sys2 as fsys;
use fidl_fuchsia_sys_internal::SourceIdentity;
use fidl_table_validation::ValidFidlTable;
use fuchsia_zircon as zx;
use std::{convert::TryFrom, ops::Deref};

#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub struct Moniker(Vec<String>);

impl Deref for Moniker {
    type Target = Vec<String>;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl std::fmt::Display for Moniker {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.join("/"))
    }
}

impl From<Vec<&str>> for Moniker {
    fn from(other: Vec<&str>) -> Moniker {
        Moniker(other.into_iter().map(|s| s.to_string()).collect())
    }
}

impl From<Vec<String>> for Moniker {
    fn from(other: Vec<String>) -> Moniker {
        Moniker(other)
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

impl From<Vec<String>> for UniqueKey {
    fn from(other: Vec<String>) -> UniqueKey {
        UniqueKey(other)
    }
}

impl From<Vec<&str>> for UniqueKey {
    fn from(other: Vec<&str>) -> UniqueKey {
        UniqueKey(other.into_iter().map(|s| s.to_string()).collect())
    }
}

impl From<UniqueKey> for Vec<String> {
    fn from(other: UniqueKey) -> Vec<String> {
        other.0
    }
}

/// Event types that contain singleton data. When these events are cloned, their singleton data
/// won't be cloned.
#[derive(Clone, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub enum EventType {
    DiagnosticsReady,
    LogSinkRequested,
}

impl AsRef<str> for EventType {
    fn as_ref(&self) -> &str {
        match &self {
            Self::DiagnosticsReady => "diagnostics_ready",
            Self::LogSinkRequested => "log_sink_requested",
        }
    }
}

/// An event that is emitted and consumed.
#[derive(Debug, Clone)]
pub struct Event {
    /// The contents of the event.
    pub payload: EventPayload,
    pub timestamp: zx::Time,
}

impl Event {
    pub fn ty(&self) -> EventType {
        match &self.payload {
            EventPayload::DiagnosticsReady(_) => EventType::DiagnosticsReady,
            EventPayload::LogSinkRequested(_) => EventType::LogSinkRequested,
        }
    }
}

/// The contents of the event depending on the type of event.
#[derive(Debug, Clone)]
pub enum EventPayload {
    DiagnosticsReady(DiagnosticsReadyPayload),
    LogSinkRequested(LogSinkRequestedPayload),
}

/// Payload for a CapabilityReady(diagnostics) event.
#[derive(Debug)]
pub struct DiagnosticsReadyPayload {
    /// The component which diagnostics directory is available.
    pub component: ComponentIdentity,
    /// The `out/diagnostics` directory of the component.
    pub directory: Option<fio::DirectoryProxy>,
}

impl Clone for DiagnosticsReadyPayload {
    fn clone(&self) -> Self {
        Self { component: self.component.clone(), directory: None }
    }
}

/// Payload for a connection to the `LogSink` protocol.
pub struct LogSinkRequestedPayload {
    /// The component that is connecting to `LogSink`.
    pub component: ComponentIdentity,
    /// The stream containing requests made on the `LogSink` channel by the component.
    pub request_stream: Option<flogger::LogSinkRequestStream>,
}

impl Clone for LogSinkRequestedPayload {
    fn clone(&self) -> Self {
        Self { component: self.component.clone(), request_stream: None }
    }
}

impl std::fmt::Debug for LogSinkRequestedPayload {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("LogSinkRequestedPayload").field("component", &self.component).finish()
    }
}

/// A single segment in the moniker of a component.
#[derive(Debug, Clone, Eq, Hash, PartialEq)]
pub struct MonikerSegment {
    /// The name of the component's collection, if any.
    pub collection: Option<String>,
    /// The name of the component.
    pub name: String,
}

impl std::fmt::Display for MonikerSegment {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if let Some(collection) = &self.collection {
            write!(f, "{}:", collection)?;
        }
        write!(f, "{}", self.name)
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
            }]));
        }

        if moniker == "." {
            return Ok(ComponentIdentifier::Moniker(vec![MonikerSegment {
                collection: None,
                name: "<root>".to_string(),
            }]));
        }

        let without_root = moniker
            .strip_prefix("./")
            .ok_or_else(|| MonikerError::InvalidMonikerPrefix(moniker.to_string()))?;

        let mut segments = vec![];
        for raw_segment in without_root.split('/') {
            let mut parts = raw_segment.split(':');
            let segment = match (parts.next(), parts.next()) {
                // we have a component name and a collection
                (Some(c), Some(n)) => {
                    MonikerSegment { collection: Some(c.to_string()), name: n.to_string() }
                }
                // we have a component name
                (Some(n), None) => MonikerSegment { collection: None, name: n.to_string() },
                _ => return Err(MonikerError::InvalidSegment(raw_segment.to_string())),
            };
            segments.push(segment);
        }

        Ok(ComponentIdentifier::Moniker(segments))
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
#[fidl_table_src(fsys::EventHeader)]
pub struct ValidatedEventHeader {
    pub event_type: fsys::EventType,
    pub component_url: String,
    pub moniker: String,
    pub timestamp: i64,
}

#[derive(Debug, ValidFidlTable)]
#[fidl_table_src(fsys::Event)]
pub struct ValidatedEvent {
    /// Information about the component for which this event was generated.
    pub header: ValidatedEventHeader,

    /// Optional payload for some event types
    #[fidl_field_type(optional)]
    pub event_result: Option<fsys::EventResult>,
}

impl TryFrom<fsys::Event> for Event {
    type Error = EventError;

    fn try_from(event: fsys::Event) -> Result<Event, Self::Error> {
        let event: ValidatedEvent = ValidatedEvent::try_from(event)?;

        let identity = ComponentIdentity::from_identifier_and_url(
            ComponentIdentifier::parse_from_moniker(&event.header.moniker)?,
            &event.header.component_url,
        );

        match event.header.event_type {
            fsys::EventType::DirectoryReady => {
                let directory = match event.event_result {
                    Some(fsys::EventResult::Payload(fsys::EventPayload::DirectoryReady(
                        directory_ready,
                    ))) => {
                        let name = directory_ready.name.ok_or(EventError::MissingField("name"))?;
                        if name != "diagnostics" {
                            return Err(EventError::IncorrectName {
                                received: name,
                                expected: "diagnostics",
                            });
                        }
                        match directory_ready.node {
                            Some(node) => fuchsia_fs::node_to_directory(node.into_proxy()?)
                                .map_err(EventError::NodeToDirectory)?,
                            None => return Err(EventError::MissingDiagnosticsDir),
                        }
                    }
                    None => return Err(EventError::ExpectedResult),
                    Some(result) => return Err(EventError::UnknownResult(result)),
                };
                Ok(Event {
                    timestamp: zx::Time::from_nanos(event.header.timestamp),
                    payload: EventPayload::DiagnosticsReady(DiagnosticsReadyPayload {
                        component: identity,
                        directory: Some(directory),
                    }),
                })
            }
            fsys::EventType::CapabilityRequested => {
                let request_stream = match event.event_result {
                    Some(fsys::EventResult::Payload(fsys::EventPayload::CapabilityRequested(
                        capability_requested,
                    ))) => {
                        let name =
                            capability_requested.name.ok_or(EventError::MissingField("name"))?;

                        if name != flogger::LogSinkMarker::PROTOCOL_NAME {
                            return Err(EventError::IncorrectName {
                                received: name,
                                expected: flogger::LogSinkMarker::PROTOCOL_NAME,
                            });
                        }
                        let capability = capability_requested
                            .capability
                            .ok_or(EventError::MissingField("capability"))?;
                        ServerEnd::<flogger::LogSinkMarker>::new(capability).into_stream()?
                    }
                    None => return Err(EventError::ExpectedResult),
                    Some(result) => return Err(EventError::UnknownResult(result)),
                };
                Ok(Event {
                    timestamp: zx::Time::from_nanos(event.header.timestamp),
                    payload: EventPayload::LogSinkRequested(LogSinkRequestedPayload {
                        component: identity,
                        request_stream: Some(request_stream),
                    }),
                })
            }
            _ => Err(EventError::InvalidEventType(event.header.event_type)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia::test]
    fn convert_v2_moniker_for_diagnostics() {
        let identifier = ComponentIdentifier::parse_from_moniker("./a").unwrap();
        assert_eq!(identifier.relative_moniker_for_selectors(), vec!["a"].into());
        assert_eq!(identifier.unique_key(), vec!["a"].into());

        let identifier = ComponentIdentifier::parse_from_moniker("./a/b").unwrap();
        assert_eq!(identifier.relative_moniker_for_selectors(), vec!["a", "b"].into());
        assert_eq!(identifier.unique_key(), vec!["a", "b"].into());

        let identifier = ComponentIdentifier::parse_from_moniker("./a/coll:comp/b").unwrap();
        assert_eq!(identifier.relative_moniker_for_selectors(), vec!["a", "coll:comp", "b"].into());
        assert_eq!(identifier.unique_key(), vec!["a", "coll:comp", "b"].into());

        let identifier = ComponentIdentifier::parse_from_moniker(".").unwrap();
        assert_eq!(identifier.relative_moniker_for_selectors(), vec!["<root>"].into());
        assert_eq!(identifier.unique_key(), vec!["<root>"].into());
    }
}
