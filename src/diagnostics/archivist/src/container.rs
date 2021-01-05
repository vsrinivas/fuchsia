// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use {
    crate::{
        events::types::ComponentIdentifier, inspect::container::InspectArtifactsContainer,
        lifecycle::container::LifecycleArtifactsContainer,
    },
    diagnostics_data as schema,
    diagnostics_hierarchy::DiagnosticsHierarchy,
    fuchsia_async as fasync,
    fuchsia_inspect::reader::snapshot::{Snapshot, SnapshotTree},
    fuchsia_zircon as zx,
    std::sync::Arc,
};

pub enum ReadSnapshot {
    Single(Snapshot),
    Tree(SnapshotTree),
    Finished(DiagnosticsHierarchy),
}

pub struct ComponentIdentity {
    /// Relative moniker of the component that this artifacts container
    /// is representing.
    pub relative_moniker: Vec<String>,

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
            unique_key: identifier.unique_key(),
            url: url.into(),
        }
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
