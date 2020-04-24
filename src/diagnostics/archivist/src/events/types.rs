// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_inspect::TreeProxy,
    fidl_fuchsia_inspect_deprecated::InspectProxy,
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys2 as fsys,
    fidl_fuchsia_sys_internal::SourceIdentity,
    fuchsia_zircon as zx,
    futures::{channel::mpsc, stream::BoxStream},
    io_util,
    std::{
        collections::HashMap,
        convert::TryFrom,
        ops::{Deref, DerefMut},
    },
};

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

#[derive(Debug)]
pub struct InspectReaderData {
    /// The identifier of this component
    pub component_id: ComponentIdentifier,

    /// Proxy to the inspect data host.
    pub data_directory_proxy: Option<DirectoryProxy>,
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

impl TryFrom<SourceIdentity> for ComponentIdentifier {
    type Error = anyhow::Error;

    fn try_from(component: SourceIdentity) -> Result<Self, Self::Error> {
        if component.component_name.is_some()
            && component.instance_id.is_some()
            && component.realm_path.is_some()
        {
            Ok(ComponentIdentifier::Legacy(LegacyIdentifier {
                component_name: component.component_name.unwrap(),
                instance_id: component.instance_id.unwrap(),
                realm_path: RealmPath(component.realm_path.unwrap()),
            }))
        } else {
            Err(format_err!("Missing fields in SourceIdentity"))
        }
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

/// Represents the data associated with a component event.
#[derive(Debug)]
pub struct ComponentEventData {
    pub component_id: ComponentIdentifier,

    /// Extra data about this event (to be stored in extra files in the archive).
    pub component_data_map: Option<HashMap<String, InspectData>>,
}

pub type ComponentEventChannel = mpsc::Sender<ComponentEvent>;

/// A stream of |ComponentEvent|s
pub type ComponentEventStream = BoxStream<'static, ComponentEvent>;

/// An event that occurred to a component.
#[derive(Debug)]
pub enum ComponentEvent {
    /// We observed the component starting.
    Start(ComponentEventData),

    /// We observed the component stopping.
    Stop(ComponentEventData),

    /// We observed the creation of a new `out` directory.
    OutDirectoryAppeared(InspectReaderData),
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

impl TryFrom<fsys::Event> for ComponentEvent {
    type Error = anyhow::Error;

    fn try_from(event: fsys::Event) -> Result<ComponentEvent, Error> {
        if event.target_moniker.is_none() {
            return Err(format_err!("No moniker present"));
        }
        match event.event_type {
            Some(fsys::EventType::Started) | Some(fsys::EventType::Running) => {
                let data = ComponentEventData {
                    component_id: ComponentIdentifier::Moniker(event.target_moniker.unwrap()),
                    component_data_map: None,
                };
                Ok(ComponentEvent::Start(data))
            }
            Some(fsys::EventType::Stopped) => {
                let data = ComponentEventData {
                    component_id: ComponentIdentifier::Moniker(event.target_moniker.unwrap()),
                    component_data_map: None,
                };
                Ok(ComponentEvent::Stop(data))
            }
            Some(fsys::EventType::CapabilityReady) => {
                if let Some(node_proxy) = event
                    .event_result
                    .and_then(|result| {
                        match result {
                            fsys::EventResult::Payload(payload) => payload.capability_ready,
                            fsys::EventResult::Error(_) => {
                                // TODO: result.error carries information about errors that happened
                                // in component_manager. We should dump those in diagnostics.
                                None
                            }
                            _ => None,
                        }
                    })
                    .and_then(|capability_ready| {
                        if capability_ready.path == Some("/diagnostics".to_string()) {
                            capability_ready.node
                        } else {
                            None
                        }
                    })
                {
                    let data = InspectReaderData {
                        component_id: ComponentIdentifier::Moniker(event.target_moniker.unwrap()),
                        data_directory_proxy: io_util::node_to_directory(node_proxy.into_proxy()?)
                            .ok(),
                    };
                    return Ok(ComponentEvent::OutDirectoryAppeared(data));
                }
                Err(format_err!("Missing diagnostics directory in CapabilityReady payload"))
            }
            _ => Err(format_err!("Unexpected type: {:?}", event.event_type)),
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
            (ComponentEvent::OutDirectoryAppeared(a), ComponentEvent::OutDirectoryAppeared(b)) => {
                return a == b;
            }
            _ => false,
        }
    }
}

impl PartialEq for ComponentEventData {
    /// Check ComponentEventData for equality.
    ///
    /// We implement this manually so that we can avoid requiring equality comparison on
    /// `component_data_map`.
    fn eq(&self, other: &Self) -> bool {
        self.component_id == other.component_id
    }
}

impl PartialEq for InspectReaderData {
    fn eq(&self, other: &Self) -> bool {
        self.component_id == other.component_id
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
    }
}
