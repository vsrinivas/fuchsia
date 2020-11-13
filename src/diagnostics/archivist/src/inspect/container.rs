// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::collector::InspectDataCollector,
    crate::{
        container::{ReadSnapshot, SnapshotData},
        events::types::InspectData,
    },
    diagnostics_data::{self as schema},
    fidl_fuchsia_io::DirectoryProxy,
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_inspect::reader::snapshot::{Snapshot, SnapshotTree},
    fuchsia_inspect_node_hierarchy::InspectHierarchyMatcher,
    fuchsia_zircon::{self as zx, DurationNum},
    inspect_fidl_load as deprecated_inspect,
    log::error,
    std::convert::TryFrom,
    std::sync::Arc,
};

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
    async fn from(
        unpopulated: Arc<UnpopulatedInspectDataContainer>,
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
                        relative_moniker: unpopulated.relative_moniker.clone(),
                        component_url: unpopulated.component_url.clone(),
                        inspect_matcher: unpopulated.inspect_matcher.clone(),
                        snapshots,
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
                            relative_moniker: unpopulated.relative_moniker.clone(),
                            component_url: unpopulated.component_url.clone(),
                            snapshots: no_success_snapshot_data,
                            inspect_matcher: unpopulated.inspect_matcher.clone(),
                        }
                    }
                }
            }
            Err(e) => {
                let no_success_snapshot_data = vec![SnapshotData::failed(
                    schema::Error {
                        message: format!(
                            "Encountered error traversing diagnostics dir for {:?}: {:?}",
                            &unpopulated.relative_moniker, e
                        ),
                    },
                    "NO_FILE_SUCCEEDED".to_string(),
                )];
                PopulatedInspectDataContainer {
                    relative_moniker: unpopulated.relative_moniker.clone(),
                    component_url: unpopulated.component_url.clone(),
                    snapshots: no_success_snapshot_data,
                    inspect_matcher: unpopulated.inspect_matcher.clone(),
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

impl UnpopulatedInspectDataContainer {
    /// Populates this data container with a timeout. On the timeout firing returns a
    /// container suitable to return to clients, but with timeout error information recorded.
    pub async fn populate(
        self,
        timeout: i64,
        on_timeout: impl FnOnce(),
    ) -> PopulatedInspectDataContainer {
        let this = Arc::new(self);
        PopulatedInspectDataContainer::from(this.clone())
            .on_timeout(timeout.seconds().after_now(), move || {
                on_timeout();
                let error_string = format!(
                    "Exceeded per-component time limit for fetching diagnostics data: {:?}",
                    &this.relative_moniker
                );
                error!("{}", error_string);
                PopulatedInspectDataContainer {
                    component_url: this.component_url.clone(),
                    inspect_matcher: this.inspect_matcher.clone(),
                    relative_moniker: this.relative_moniker.clone(),
                    snapshots: vec![SnapshotData::failed(
                        schema::Error { message: error_string },
                        "NO_FILE_SUCCEEDED".to_string(),
                    )],
                }
            })
            .await
    }
}
