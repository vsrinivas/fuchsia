// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    async_trait::async_trait,
    fidl_fuchsia_inspect::TreeProxy,
    fidl_fuchsia_inspect_deprecated::InspectProxy,
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys2::{self as fsys, ComponentDescriptor, Event},
    fidl_fuchsia_sys_internal::SourceIdentity,
    fidl_table_validation::*,
    fuchsia_zircon as zx,
    futures::{channel::mpsc, stream::BoxStream},
    io_util,
    std::{
        convert::TryFrom,
        ops::{Deref, DerefMut},
    },
};

#[async_trait]
pub trait EventSource: Sync + Send {
    async fn listen(&self, sender: mpsc::Sender<ComponentEvent>) -> Result<(), Error>;
}

/// The capacity for bounded channels used by this implementation.
pub static CHANNEL_CAPACITY: usize = 1024;

/// A realm path is a vector of realm names.
#[derive(Clone, Eq, PartialEq, Debug)]
pub struct RealmPath(pub Vec<String>);

impl Deref for RealmPath {
    type Target = Vec<String>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for RealmPath {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl From<Vec<String>> for RealmPath {
    fn from(v: Vec<String>) -> Self {
        RealmPath(v)
    }
}

impl Into<String> for RealmPath {
    fn into(self) -> String {
        self.0.join("/").to_string()
    }
}

/// Represents the ID of a component.
#[derive(Debug, Clone, PartialEq)]
pub enum ComponentIdentifier {
    Legacy(LegacyIdentifier),
    Moniker(String),
}

impl ComponentIdentifier {
    /// Returns the relative moniker to be used for selectors.
    /// For legacy components (v1), this is the relative moniker with respect to the root realm.
    pub fn relative_moniker_for_selectors(&self) -> Vec<String> {
        match self {
            Self::Legacy(identifier) => {
                let mut moniker = identifier.realm_path.clone();
                moniker.push(identifier.component_name.clone());
                moniker.0
            }
            Self::Moniker(moniker) => {
                // Synthesis of root of hierarchy yields a `.` moniker,
                // treating it as an empty moniker works for our data
                // repository.
                if moniker == "." {
                    return Vec::new();
                }
                // Transforms moniker strings such as "a:0/b:0/coll:dynamic_child:1/c:0 into
                // a/b/coll:dynamic_child/c
                // 2.. to remove the `.`, always present as this is a relative moniker.
                moniker[2..]
                    .split("/")
                    .map(|component| match &component.split(":").collect::<Vec<_>>()[..] {
                        [collection_name, component_name, _instance_id] => {
                            format!("{}:{}", collection_name, component_name)
                        }
                        [component_name, _instance_id] => component_name.to_string(),
                        x => unreachable!("We only expect two or three parts. Got: {:?}", x),
                    })
                    .collect::<Vec<_>>()
            }
        }
    }

    pub fn unique_key(&self) -> Vec<String> {
        match self {
            Self::Legacy(identifier) => {
                let mut key = self.relative_moniker_for_selectors();
                key.push(identifier.instance_id.clone());
                key
            }
            Self::Moniker(moniker) => {
                if moniker == "." {
                    return Vec::new();
                }

                // Transforms moniker strings such as "a:0/b:0/coll:dynamic_child:1/c:0 into
                // [a, 0, b, 0, coll:dynamic_child, c]
                // 1.. to remove the `./`, always present as this is a relative moniker.
                moniker[2..]
                    .split("/")
                    .flat_map(|parts| match &parts.split(":").collect::<Vec<_>>()[..] {
                        [collection_name, component_name, instance_id] => vec![
                            format!("{}:{}", collection_name, component_name),
                            instance_id.to_string(),
                        ]
                        .into_iter(),
                        [coll, comp] => vec![coll.to_string(), comp.to_string()].into_iter(),
                        x => unreachable!("We only expect two or three parts. Got: {:?}", x),
                    })
                    .collect::<Vec<_>>()
            }
        }
    }
}

impl TryFrom<SourceIdentity> for EventMetadata {
    type Error = anyhow::Error;
    fn try_from(component: SourceIdentity) -> Result<Self, Error> {
        let component: ValidatedSourceIdentity = ValidatedSourceIdentity::try_from(component)?;
        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            component_name: component.component_name,
            instance_id: component.instance_id,
            realm_path: RealmPath(component.realm_path),
        });
        Ok(EventMetadata {
            component_id,
            component_url: component.component_url,
            timestamp: zx::Time::get_monotonic(),
        })
    }
}

impl ToString for ComponentIdentifier {
    fn to_string(&self) -> String {
        match self {
            Self::Legacy(identifier) => format!(
                "{}/{}:{}",
                identifier.realm_path.join("/"),
                identifier.component_name,
                identifier.instance_id
            ),
            Self::Moniker(moniker) => moniker.clone(),
        }
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
#[fidl_table_src(ComponentDescriptor)]
pub struct ValidatedComponentDescriptor {
    pub component_url: String,
    pub moniker: String,
}

#[derive(Debug, ValidFidlTable)]
#[fidl_table_src(Event)]
pub struct ValidatedEvent {
    /// Event type corresponding to the event
    pub event_type: fsys::EventType,

    /// Information about the component for which this event was generated.
    pub descriptor: ValidatedComponentDescriptor,

    /// Optional payload for some event types
    #[fidl_field_type(optional)]
    pub event_result: Option<fsys::EventResult>,

    /// Time when the event occurred.
    pub timestamp: i64,
}

/// The ID of a component as used in components V1.
#[derive(Debug, Clone, PartialEq)]
pub struct LegacyIdentifier {
    /// The name of the component.
    pub component_name: String,

    /// The instance ID of the component.
    pub instance_id: String,

    /// The path to the component's realm.
    pub realm_path: RealmPath,
}

/// Represents the shared data associated with
/// all component events.
#[derive(Debug, PartialEq, Clone)]
pub struct EventMetadata {
    pub component_id: ComponentIdentifier,

    pub component_url: String,

    pub timestamp: zx::Time,
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
    type Error = anyhow::Error;

    fn try_from(event: Event) -> Result<ComponentEvent, Error> {
        let event: ValidatedEvent = ValidatedEvent::try_from(event)?;

        let shared_data = EventMetadata {
            component_id: ComponentIdentifier::Moniker(event.descriptor.moniker.clone()),
            component_url: event.descriptor.component_url.clone(),
            timestamp: zx::Time::from_nanos(event.timestamp),
        };

        match event.event_type {
            fsys::EventType::Started => {
                let start_event = StartEvent { metadata: shared_data };
                Ok(ComponentEvent::Start(start_event))
            }
            fsys::EventType::Stopped => {
                let stop_event = StopEvent { metadata: shared_data };
                Ok(ComponentEvent::Stop(stop_event))
            }
            fsys::EventType::CapabilityReady | fsys::EventType::Running => {
                construct_payload_holding_component_event(event, shared_data)
            }
            _ => Err(format_err!("Unexpected type: {:?}", event.event_type)),
        }
    }
}

fn construct_payload_holding_component_event(
    event: ValidatedEvent,
    shared_data: EventMetadata,
) -> Result<ComponentEvent, Error> {
    match event.event_result {
        Some(result) => {
            match result {
                fsys::EventResult::Payload(fsys::EventPayload::CapabilityReady(
                    capability_ready,
                )) => {
                    if capability_ready.path == Some("/diagnostics".to_string()) {
                        match capability_ready.node {
                            Some(node) => {
                                let diagnostics_ready_event = DiagnosticsReadyEvent {
                                    metadata: shared_data,
                                    directory: io_util::node_to_directory(node.into_proxy()?).ok(),
                                };
                                Ok(ComponentEvent::DiagnosticsReady(diagnostics_ready_event))
                            }
                            None => Err(format_err!(
                                "Missing diagnostics directory in CapabilityReady payload"
                            )),
                        }
                    } else {
                        Err(format_err!(
                            "DiagnosticsReady event didn't encode a diagnostics directory."
                        ))
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
                        None => Err(format_err!("Running event didn't encode start timestamp.")),
                    }
                }
                fsys::EventResult::Error(e) => {
                    // TODO(fxbug.dev/53903): result.error carries information about errors that happened
                    // in component_manager. We should dump those in diagnostics.
                    Err(format_err!("Payload containing event encountered an error: {:?}", e))
                }
                _ => Err(format_err!("Encountered an unknown payload containing event")),
            }
        }
        None => {
            Err(format_err!("Cannot extract payload from an event missing results: {:?}", event))
        }
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

    #[test]
    fn convert_v2_moniker_for_diagnostics() {
        let identifier = ComponentIdentifier::Moniker("./a:0".into());
        assert_eq!(identifier.relative_moniker_for_selectors(), vec!["a"]);
        assert_eq!(identifier.unique_key(), vec!["a", "0"]);

        let identifier = ComponentIdentifier::Moniker("./a:0/b:1".into());
        assert_eq!(identifier.relative_moniker_for_selectors(), vec!["a", "b"]);
        assert_eq!(identifier.unique_key(), vec!["a", "0", "b", "1"]);

        let identifier = ComponentIdentifier::Moniker("./a:0/coll:comp:1/b:0".into());
        assert_eq!(identifier.relative_moniker_for_selectors(), vec!["a", "coll:comp", "b"]);
        assert_eq!(identifier.unique_key(), vec!["a", "0", "coll:comp", "1", "b", "0"]);

        let identifier = ComponentIdentifier::Moniker(".".into());
        assert!(identifier.relative_moniker_for_selectors().is_empty());
        assert!(identifier.unique_key().is_empty());
    }
}
