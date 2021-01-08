// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use {
    crate::{
        events::{
            error::EventError,
            types::{ComponentIdentifier, LegacyIdentifier, ValidatedSourceIdentity},
        },
        inspect::container::InspectArtifactsContainer,
        lifecycle::container::LifecycleArtifactsContainer,
    },
    diagnostics_data as schema,
    diagnostics_hierarchy::DiagnosticsHierarchy,
    fidl_fuchsia_sys_internal::SourceIdentity,
    fuchsia_async as fasync,
    fuchsia_inspect::reader::snapshot::{Snapshot, SnapshotTree},
    fuchsia_zircon as zx,
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
}

impl TryFrom<SourceIdentity> for ComponentIdentity {
    type Error = EventError;
    fn try_from(component: SourceIdentity) -> Result<Self, Self::Error> {
        let component: ValidatedSourceIdentity = ValidatedSourceIdentity::try_from(component)?;
        let id = ComponentIdentifier::Legacy(LegacyIdentifier {
            component_name: component.component_name,
            instance_id: component.instance_id,
            realm_path: component.realm_path.into(),
        });
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
}

impl ComponentDiagnostics {
    #[cfg(test)]
    pub fn empty(identity: Arc<ComponentIdentity>) -> Self {
        Self { identity, inspect: None, lifecycle: None }
    }

    pub fn new_with_lifecycle(
        identity: Arc<ComponentIdentity>,
        lifecycle: LifecycleArtifactsContainer,
    ) -> Self {
        Self { identity, inspect: None, lifecycle: Some(lifecycle) }
    }

    pub fn new_with_inspect(
        identity: Arc<ComponentIdentity>,
        inspect: InspectArtifactsContainer,
    ) -> Self {
        Self { identity, inspect: Some(inspect), lifecycle: None }
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
