// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use {
    crate::{events::types::InspectData, inspect::collector::InspectDataCollector},
    diagnostics_data::{self as schema, LifecycleType},
    fidl_fuchsia_io::DirectoryProxy,
    fuchsia_async::{self as fasync},
    fuchsia_inspect::reader::snapshot::{Snapshot, SnapshotTree},
    fuchsia_inspect_node_hierarchy::{InspectHierarchyMatcher, NodeHierarchy, Property},
    fuchsia_zircon::{self as zx},
    inspect_fidl_load as deprecated_inspect,
    std::convert::TryFrom,
    std::sync::Arc,
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

pub struct LifecycleArtifactsContainer {
    // The time when the Start|Existing event that
    // caused the instantiation of the LifecycleArtifactsContainer
    // was created.
    pub event_timestamp: zx::Time,
    // Optional time when the component who the instantiating lifecycle
    // event was about was started. If None, it is the same as the
    // event_timestamp.
    pub component_start_time: Option<zx::Time>,
}

pub struct InspectArtifactsContainer {
    /// DirectoryProxy for the out directory that this
    /// data packet is configured for.
    pub component_diagnostics_proxy: Arc<DirectoryProxy>,
    /// Optional hierarchy matcher. If unset, the reader is running
    /// in all-access mode, meaning no matching or filtering is required.
    pub inspect_matcher: Option<InspectHierarchyMatcher>,
    /// The time when the DiagnosticsReady event that caused the creation of
    /// the inspect artifact container was created.
    pub event_timestamp: zx::Time,
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

/// LifecycleDataContainer holds all the information,
/// both metadata and payload, needed to populate a
/// snapshotted Lifecycle schema.
pub struct LifecycleDataContainer {
    pub relative_moniker: Vec<String>,
    pub payload: Option<NodeHierarchy>,
    pub component_url: String,
    pub event_timestamp: zx::Time,
    pub lifecycle_type: schema::LifecycleType,
}

impl LifecycleDataContainer {
    pub fn from_inspect_artifact(
        artifact: &InspectArtifactsContainer,
        relative_moniker: Vec<String>,
        component_url: String,
    ) -> Self {
        LifecycleDataContainer {
            relative_moniker,
            component_url,
            payload: None,
            event_timestamp: artifact.event_timestamp,
            lifecycle_type: LifecycleType::DiagnosticsReady,
        }
    }

    pub fn from_lifecycle_artifact(
        artifact: &LifecycleArtifactsContainer,
        relative_moniker: Vec<String>,
        component_url: String,
    ) -> Self {
        if let Some(component_start_time) = artifact.component_start_time {
            let payload = NodeHierarchy::new(
                "root",
                vec![Property::Int(
                    "component_start_time".to_string(),
                    component_start_time.into_nanos(),
                )],
                vec![],
            );

            LifecycleDataContainer {
                relative_moniker,
                component_url,
                payload: Some(payload),
                event_timestamp: artifact.event_timestamp,
                lifecycle_type: LifecycleType::Running,
            }
        } else {
            LifecycleDataContainer {
                relative_moniker,
                component_url,
                payload: None,
                event_timestamp: artifact.event_timestamp,
                lifecycle_type: LifecycleType::Started,
            }
        }
    }
}
/// PopulatedInspectDataContainer is the container that
/// holds the actual Inspect data for a given component,
/// along with all information needed to transform that data
/// to be returned to the client.
pub struct PopulatedInspectDataContainer {
    /// Relative moniker of the component that this populated
    /// data packet has gathered data for.
    pub relative_moniker: Vec<String>,
    /// The url with which the associated component was launched.
    pub component_url: String,
    /// Vector of all the snapshots of inspect hierarchies under
    /// the diagnostics directory of the component identified by
    /// relative_moniker, along with the metadata needed to populate
    /// this snapshot's diagnostics schema.
    pub snapshots: Vec<SnapshotData>,
    /// Optional hierarchy matcher. If unset, the reader is running
    /// in all-access mode, meaning no matching or filtering is required.
    pub inspect_matcher: Option<InspectHierarchyMatcher>,
}

impl PopulatedInspectDataContainer {
    pub async fn from(
        unpopulated: UnpopulatedInspectDataContainer,
    ) -> PopulatedInspectDataContainer {
        let mut collector = InspectDataCollector::new();

        match collector.populate_data_map(&unpopulated.component_diagnostics_proxy).await {
            Ok(_) => {
                let mut snapshots_data_opt = None;
                if let Some(data_map) = Box::new(collector).take_data() {
                    let mut acc: Vec<SnapshotData> = vec![];
                    for (filename, data) in data_map {
                        match data {
                            InspectData::Tree(tree, _) => match SnapshotTree::try_from(&tree).await
                            {
                                Ok(snapshot_tree) => {
                                    acc.push(SnapshotData::successful(
                                        ReadSnapshot::Tree(snapshot_tree),
                                        filename,
                                    ));
                                }
                                Err(e) => {
                                    acc.push(SnapshotData::failed(
                                        schema::Error { message: format!("{:?}", e) },
                                        filename,
                                    ));
                                }
                            },
                            InspectData::DeprecatedFidl(inspect_proxy) => {
                                match deprecated_inspect::load_hierarchy(inspect_proxy).await {
                                    Ok(hierarchy) => {
                                        acc.push(SnapshotData::successful(
                                            ReadSnapshot::Finished(hierarchy),
                                            filename,
                                        ));
                                    }
                                    Err(e) => {
                                        acc.push(SnapshotData::failed(
                                            schema::Error { message: format!("{:?}", e) },
                                            filename,
                                        ));
                                    }
                                }
                            }
                            InspectData::Vmo(vmo) => match Snapshot::try_from(&vmo) {
                                Ok(snapshot) => {
                                    acc.push(SnapshotData::successful(
                                        ReadSnapshot::Single(snapshot),
                                        filename,
                                    ));
                                }
                                Err(e) => {
                                    acc.push(SnapshotData::failed(
                                        schema::Error { message: format!("{:?}", e) },
                                        filename,
                                    ));
                                }
                            },
                            InspectData::File(contents) => match Snapshot::try_from(contents) {
                                Ok(snapshot) => {
                                    acc.push(SnapshotData::successful(
                                        ReadSnapshot::Single(snapshot),
                                        filename,
                                    ));
                                }
                                Err(e) => {
                                    acc.push(SnapshotData::failed(
                                        schema::Error { message: format!("{:?}", e) },
                                        filename,
                                    ));
                                }
                            },
                            InspectData::Empty => {}
                        }
                    }
                    snapshots_data_opt = Some(acc);
                }
                match snapshots_data_opt {
                    Some(snapshots) => PopulatedInspectDataContainer {
                        relative_moniker: unpopulated.relative_moniker,
                        component_url: unpopulated.component_url,
                        snapshots: snapshots,
                        inspect_matcher: unpopulated.inspect_matcher,
                    },
                    None => {
                        let no_success_snapshot_data = vec![SnapshotData::failed(
                            schema::Error {
                                message: format!(
                                    "Failed to extract any inspect data for {:?}",
                                    unpopulated.relative_moniker
                                ),
                            },
                            "NO_FILE_SUCCEEDED".to_string(),
                        )];
                        PopulatedInspectDataContainer {
                            relative_moniker: unpopulated.relative_moniker,
                            component_url: unpopulated.component_url,
                            snapshots: no_success_snapshot_data,
                            inspect_matcher: unpopulated.inspect_matcher,
                        }
                    }
                }
            }
            Err(e) => {
                let no_success_snapshot_data = vec![SnapshotData::failed(
                    schema::Error {
                        message: format!(
                            "Encountered error traversing diagnostics dir for {:?}: {:?}",
                            unpopulated.relative_moniker, e
                        ),
                    },
                    "NO_FILE_SUCCEEDED".to_string(),
                )];
                PopulatedInspectDataContainer {
                    relative_moniker: unpopulated.relative_moniker,
                    component_url: unpopulated.component_url,
                    snapshots: no_success_snapshot_data,
                    inspect_matcher: unpopulated.inspect_matcher,
                }
            }
        }
    }
}

/// UnpopulatedInspectDataContainer is the container that holds
/// all information needed to retrieve Inspect data
/// for a given component, when requested.
pub struct UnpopulatedInspectDataContainer {
    /// Relative moniker of the component that this data container
    /// is representing.
    pub relative_moniker: Vec<String>,
    /// The url with which the associated component was launched.
    pub component_url: String,
    /// DirectoryProxy for the out directory that this
    /// data packet is configured for.
    pub component_diagnostics_proxy: DirectoryProxy,
    /// Optional hierarchy matcher. If unset, the reader is running
    /// in all-access mode, meaning no matching or filtering is required.
    pub inspect_matcher: Option<InspectHierarchyMatcher>,
}
