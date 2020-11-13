// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use {
    crate::{
        inspect::container::InspectArtifactsContainer,
        lifecycle::container::LifecycleArtifactsContainer,
    },
    diagnostics_data::{self as schema},
    fuchsia_async::{self as fasync},
    fuchsia_inspect::reader::snapshot::{Snapshot, SnapshotTree},
    fuchsia_inspect_node_hierarchy::NodeHierarchy,
    fuchsia_zircon::{self as zx},
};

pub enum ReadSnapshot {
    Single(Snapshot),
    Tree(SnapshotTree),
    Finished(NodeHierarchy),
}

pub struct DiagnosticsArtifactsContainer {
    /// Relative moniker of the component that this artifacts container
    /// is representing.
    pub relative_moniker: Vec<String>,
    /// The url with which the associated component was launched.
    pub component_url: String,
    /// Container holding the artifacts needed to serve inspect data.
    /// If absent, this is interpereted as a component existing, but not
    /// hosting diagnostics data.
    pub inspect_artifacts_container: Option<InspectArtifactsContainer>,
    /// Container holding the artifacts needed to serve lifecycle data.
    pub lifecycle_artifacts_container: Option<LifecycleArtifactsContainer>,
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
