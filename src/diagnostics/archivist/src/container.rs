// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use {
    crate::{
        events::{
            error::EventError,
            types::{ComponentIdentifier, ValidatedSourceIdentity},
        },
        inspect::container::InspectArtifactsContainer,
        lifecycle::container::LifecycleArtifactsContainer,
        logs::{
            buffer::AccountedBuffer, container::LogsArtifactsContainer, stats::LogStreamStats,
            Message,
        },
    },
    diagnostics_data as schema,
    diagnostics_hierarchy::DiagnosticsHierarchy,
    fidl_fuchsia_logger::LogInterestSelector,
    fidl_fuchsia_sys_internal::SourceIdentity,
    fuchsia_async as fasync,
    fuchsia_inspect::reader::snapshot::{Snapshot, SnapshotTree},
    fuchsia_inspect_derive::WithInspect,
    fuchsia_zircon as zx,
    parking_lot::Mutex,
    std::{convert::TryFrom, sync::Arc},
};

pub enum ReadSnapshot {
    Single(Snapshot),
    Tree(SnapshotTree),
    Finished(DiagnosticsHierarchy),
}

#[derive(Clone, Debug, PartialEq)]
pub struct ComponentIdentity {
    /// Relative moniker of the component that this artifacts container
    /// is representing.
    pub relative_moniker: Vec<String>,

    /// A "rendered" moniker has had `/` inserted between segments and has had instance IDs
    /// appended after colons, this is the form of moniker most familiar to downstream users.
    pub rendered_moniker: String,

    /// The url with which the associated component was launched.
    pub url: String,

    /// In V1, a component topology is able to produce two components with
    /// the same relative moniker. Because of this, we must, in some cases,
    /// differentiate these components using instance ids. The unique key
    /// is conceptually a relative moniker which preserves instance ids.
    pub unique_key: Vec<String>,
}

impl ComponentIdentity {
    pub fn from_identifier_and_url(
        identifier: &ComponentIdentifier,
        url: impl Into<String>,
    ) -> Self {
        ComponentIdentity {
            relative_moniker: identifier.relative_moniker_for_selectors(),
            rendered_moniker: identifier.to_string(),
            unique_key: identifier.unique_key(),
            url: url.into(),
        }
    }

    /// Returns generic metadata, suitable for providing a uniform ID to unattributed data.
    pub fn unknown() -> Self {
        Self::from_identifier_and_url(
            &ComponentIdentifier::Legacy {
                component_name: "UNKNOWN".into(),
                instance_id: "0".to_string(),
                realm_path: vec![].into(),
            },
            "fuchsia-pkg://UNKNOWN",
        )
    }
}

impl TryFrom<SourceIdentity> for ComponentIdentity {
    type Error = EventError;
    fn try_from(component: SourceIdentity) -> Result<Self, Self::Error> {
        let component: ValidatedSourceIdentity = ValidatedSourceIdentity::try_from(component)?;
        let id = ComponentIdentifier::Legacy {
            component_name: component.component_name,
            instance_id: component.instance_id,
            realm_path: component.realm_path.into(),
        };
        Ok(Self::from_identifier_and_url(&id, component.component_url))
    }
}

impl std::fmt::Display for ComponentIdentity {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", &self.rendered_moniker)
    }
}

/// Holds all diagnostics data artifacts for a given component.
///
/// While all members are public for convenience, other data types may be added in the future and
/// so this is marked with `#[non_exhaustive]`.
#[non_exhaustive]
pub struct ComponentDiagnostics {
    /// Container holding the artifacts needed to uniquely identify
    /// a component on the system.
    pub identity: Arc<ComponentIdentity>,
    /// Container holding the artifacts needed to serve inspect data.
    /// If absent, this is interpereted as a component existing, but not
    /// hosting diagnostics data.
    pub inspect: Option<InspectArtifactsContainer>,
    /// Container holding the artifacts needed to serve lifecycle data.
    pub lifecycle: Option<LifecycleArtifactsContainer>,
    /// Container holding cached log messages and interest dispatchers.
    pub logs: Option<Arc<LogsArtifactsContainer>>,
    /// Holds the state for `root/sources/MONIKER/*` in Archivist's inspect.
    pub source_node: fuchsia_inspect::Node,
}

impl ComponentDiagnostics {
    pub fn empty(identity: Arc<ComponentIdentity>, parent: &fuchsia_inspect::Node) -> Self {
        let source_node = parent.create_child(identity.relative_moniker.join("/"));
        source_node.record_string("url", &identity.url);
        Self { identity, inspect: None, lifecycle: None, logs: None, source_node }
    }

    pub fn new_with_lifecycle(
        identity: Arc<ComponentIdentity>,
        lifecycle: LifecycleArtifactsContainer,
        parent: &fuchsia_inspect::Node,
    ) -> Self {
        let mut new = Self::empty(identity, parent);
        new.lifecycle = Some(lifecycle);
        new
    }

    pub fn new_with_inspect(
        identity: Arc<ComponentIdentity>,
        inspect: InspectArtifactsContainer,
        parent: &fuchsia_inspect::Node,
    ) -> Self {
        let mut new = Self::empty(identity, parent);
        new.inspect = Some(inspect);
        new
    }

    pub fn logs(
        &mut self,
        // TODO(fxbug.dev/47611) remove this and construct a local buffer in this function
        buffer: &Arc<Mutex<AccountedBuffer<Message>>>,
        interest_selectors: &[LogInterestSelector],
    ) -> Arc<LogsArtifactsContainer> {
        if let Some(logs) = &self.logs {
            logs.clone()
        } else {
            let stats = LogStreamStats::default()
                .with_inspect(&self.source_node, "logs")
                .expect("failed to attach component log stats");
            let container = Arc::new(LogsArtifactsContainer::new(
                self.identity.clone(),
                interest_selectors,
                stats,
                buffer.clone(),
            ));
            self.logs = Some(container.clone());
            container
        }
    }
}

/// Packet containing a snapshot and all the metadata needed to
/// populate a diagnostics schema for that snapshot.
pub struct SnapshotData {
    // Name of the file that created this snapshot.
    pub filename: String,
    // Timestamp at which this snapshot resolved or failed.
    pub timestamp: zx::Time,
    // Errors encountered when processing this snapshot.
    pub errors: Vec<schema::Error>,
    // Optional snapshot of the inspect hierarchy, in case reading fails
    // and we have errors to share with client.
    pub snapshot: Option<ReadSnapshot>,
}

impl SnapshotData {
    // Constructs packet that timestamps and packages inspect snapshot for exfiltration.
    pub fn successful(snapshot: ReadSnapshot, filename: String) -> SnapshotData {
        SnapshotData {
            filename,
            timestamp: fasync::Time::now().into_zx(),
            errors: Vec::new(),
            snapshot: Some(snapshot),
        }
    }

    // Constructs packet that timestamps and packages inspect snapshot failure for exfiltration.
    pub fn failed(error: schema::Error, filename: String) -> SnapshotData {
        SnapshotData {
            filename,
            timestamp: fasync::Time::now().into_zx(),
            errors: vec![error],
            snapshot: None,
        }
    }
}
