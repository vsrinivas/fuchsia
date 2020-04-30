// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        events::types::{ComponentIdentifier, InspectData},
        formatter::{self, JsonInspectSchema},
    },
    anyhow::{format_err, Error},
    fidl::endpoints::DiscoverableService,
    fidl::endpoints::{RequestStream, ServerEnd},
    fidl_fuchsia_diagnostics::{self, BatchIteratorMarker, BatchIteratorRequestStream, Selector},
    fidl_fuchsia_inspect::TreeMarker,
    fidl_fuchsia_inspect_deprecated::InspectMarker,
    fidl_fuchsia_io::{DirectoryProxy, NodeInfo, CLONE_FLAG_SAME_RIGHTS},
    fidl_fuchsia_mem, files_async,
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_inspect::reader::{
        snapshot::{Snapshot, SnapshotTree},
        PartialNodeHierarchy,
    },
    fuchsia_inspect_node_hierarchy::{
        trie::{self, TrieIterableNode},
        InspectHierarchyMatcher, NodeHierarchy,
    },
    fuchsia_zircon::{self as zx, DurationNum, HandleBased},
    futures::future::{join_all, BoxFuture},
    futures::stream::{FusedStream, StreamExt},
    futures::{FutureExt, TryFutureExt, TryStreamExt},
    inspect_fidl_load as deprecated_inspect, io_util,
    log::error,
    parking_lot::{Mutex, RwLock},
    pin_utils::pin_mut,
    selectors,
    std::collections::HashMap,
    std::convert::{TryFrom, TryInto},
    std::path::{Path, PathBuf},
    std::sync::Arc,
};

/// Keep only 64 hierarchy snapshots in memory at a time.
/// We limit to 64 because each snapshot is sent over a VMO and we can only have
/// 64 handles sent over a message.
// TODO(4601): Make this product-configurable.
// TODO(4601): Consider configuring batch sizes by bytes, not by hierarchy count.
static IN_MEMORY_SNAPSHOT_LIMIT: usize = 64;

// Number of seconds to wait before timing out various async operations;
//    1) Reading the diagnostics directory of a component, searching for inspect files.
//    2) Getting the description of a file proxy backing the inspect data.
//    3) Reading the bytes from a File.
//    4) Loading a hierachy from the deprecated inspect fidl protocol.
//    5) Converting an unpopulated data map into a populated data map.
static INSPECT_ASYNC_TIMEOUT_SECONDS: i64 = 5;

type InspectDataTrie = trie::Trie<String, UnpopulatedInspectDataContainer>;

enum ReadSnapshot {
    Single(Snapshot),
    Tree(SnapshotTree),
    Finished(NodeHierarchy),
}

/// Mapping from a diagnostics filename to the underlying encoding of that
/// diagnostics data.
type DataMap = HashMap<String, InspectData>;

type Moniker = String;

pub trait DataCollector {
    // Processes all previously collected data from the configured sources,
    // provides the returned DataMap with ownership of that data, returns the
    // map, and clears the collector state.
    //
    // If no data has yet been collected, or if the data had previously been
    // collected, then the return value will be None.
    fn take_data(self: Box<Self>) -> Option<DataMap>;

    // Triggers the process of collection, causing the collector to find and stage
    // all data it is configured to collect for transfer of ownership by the next
    // take_data call.
    fn collect(self: Box<Self>, path: PathBuf) -> BoxFuture<'static, Result<(), Error>>;
}

/// InspectDataCollector holds the information needed to retrieve the Inspect
/// VMOs associated with a particular component
#[derive(Clone, Debug)]
pub struct InspectDataCollector {
    /// The inspect data associated with a particular event.
    ///
    /// This is wrapped in an Arc Mutex so it can be shared between multiple data sources.
    ///
    /// Note: The Arc is needed so that we can both add the data map to a data collector
    ///       and trigger async collection of the data in the same method. This can only
    ///       be done by allowing the async method to populate the same data that is being
    ///       passed into the component event.
    inspect_data_map: Arc<Mutex<Option<DataMap>>>,
}

impl InspectDataCollector {
    /// Construct a new InspectDataCollector, wrapped by an Arc<Mutex>.
    pub fn new() -> Self {
        InspectDataCollector { inspect_data_map: Arc::new(Mutex::new(Some(DataMap::new()))) }
    }

    /// Convert a fully-qualified path to a directory-proxy in the executing namespace.
    /// NOTE: Currently does a synchronous directory-open, since there are no available
    ///       async apis.
    pub async fn find_directory_proxy(path: &Path) -> Result<DirectoryProxy, Error> {
        // TODO(36762): When available, use the async directory-open api.
        return io_util::open_directory_in_namespace(
            &path.to_string_lossy(),
            io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
        );
    }

    /// Searches the directory specified by inspect_directory_proxy for
    /// .inspect files and populates the `inspect_data_map` with the found VMOs.
    pub async fn populate_data_map(&mut self, inspect_proxy: &DirectoryProxy) -> Result<(), Error> {
        // TODO(36762): Use a streaming and bounded readdir API when available to avoid
        // being hung.
        let entries = files_async::readdir_recursive(
            inspect_proxy,
            Some(INSPECT_ASYNC_TIMEOUT_SECONDS.seconds()),
        )
        .filter_map(|result| {
            async move {
                // TODO(fxb/49157): decide how to show directories that we failed to read.
                result.ok()
            }
        });
        pin_mut!(entries);
        while let Some(entry) = entries.next().await {
            // We are only currently interested in inspect VMO files (root.inspect) and
            // inspect services.
            if let Some(proxy) = self.maybe_load_service::<TreeMarker>(inspect_proxy, &entry)? {
                let maybe_vmo = proxy
                    .get_content()
                    .err_into::<anyhow::Error>()
                    .on_timeout(INSPECT_ASYNC_TIMEOUT_SECONDS.seconds().after_now(), || {
                        Err(format_err!("Timed out reading contents via Tree protocol."))
                    })
                    .await?
                    .buffer
                    .map(|b| b.vmo);

                self.maybe_add(&entry.name, InspectData::Tree(proxy, maybe_vmo));
                continue;
            }

            if let Some(proxy) = self.maybe_load_service::<InspectMarker>(inspect_proxy, &entry)? {
                self.maybe_add(&entry.name, InspectData::DeprecatedFidl(proxy));
                continue;
            }

            if !entry.name.ends_with(".inspect") || entry.kind != files_async::DirentKind::File {
                continue;
            }

            let file_proxy = match io_util::open_file(
                inspect_proxy,
                Path::new(&entry.name),
                io_util::OPEN_RIGHT_READABLE,
            ) {
                Ok(proxy) => proxy,
                Err(_) => {
                    continue;
                }
            };

            // Obtain the vmo backing any VmoFiles.
            match file_proxy
                .describe()
                .err_into::<anyhow::Error>()
                .on_timeout(INSPECT_ASYNC_TIMEOUT_SECONDS.seconds().after_now(), || {
                    Err(format_err!(
                        "Timed out waiting for backing file description: {:?}",
                        file_proxy
                    ))
                })
                .await
            {
                Ok(nodeinfo) => match nodeinfo {
                    NodeInfo::Vmofile(vmofile) => {
                        self.maybe_add(&entry.name, InspectData::Vmo(vmofile.vmo));
                    }
                    NodeInfo::File(_) => {
                        let contents = io_util::read_file_bytes(&file_proxy)
                            .on_timeout(INSPECT_ASYNC_TIMEOUT_SECONDS.seconds().after_now(), || {
                                Err(format_err!(
                                    "Timed out reading contents of fuchsia File: {:?}",
                                    file_proxy
                                ))
                            })
                            .await?;
                        self.maybe_add(&entry.name, InspectData::File(contents));
                    }
                    ty @ _ => {
                        error!(
                            "found an inspect file '{}' of unexpected type {:?}",
                            &entry.name, ty
                        );
                    }
                },
                Err(_) => {}
            }
        }

        Ok(())
    }

    /// Adds a key value to the contained vector if it hasn't been taken yet. Otherwise, does
    /// nothing.
    fn maybe_add(&mut self, key: impl Into<String>, value: InspectData) {
        if let Some(map) = self.inspect_data_map.lock().as_mut() {
            map.insert(key.into(), value);
        };
    }

    fn maybe_load_service<S: DiscoverableService>(
        &self,
        dir_proxy: &DirectoryProxy,
        entry: &files_async::DirEntry,
    ) -> Result<Option<S::Proxy>, Error> {
        if entry.name.ends_with(S::SERVICE_NAME) {
            if entry.kind != files_async::DirentKind::Service {
                return Ok(None);
            }
            let (proxy, server) = fidl::endpoints::create_proxy::<S>()?;
            fdio::service_connect_at(dir_proxy.as_ref(), &entry.name, server.into_channel())?;
            return Ok(Some(proxy));
        }
        Ok(None)
    }
}

impl DataCollector for InspectDataCollector {
    /// Takes the contained extra data. Additions following this have no effect.
    fn take_data(self: Box<Self>) -> Option<DataMap> {
        self.inspect_data_map.lock().take()
    }

    /// Collect extra data stored under the given path.
    ///
    /// This currently only does a single pass over the directory to find information.
    fn collect(mut self: Box<Self>, path: PathBuf) -> BoxFuture<'static, Result<(), Error>> {
        async move {
            let inspect_proxy = match InspectDataCollector::find_directory_proxy(&path)
                .on_timeout(INSPECT_ASYNC_TIMEOUT_SECONDS.seconds().after_now(), || {
                    Err(format_err!("Timed out converting path into directory proxy: {:?}", path))
                })
                .await
            {
                Ok(proxy) => proxy,
                Err(e) => {
                    return Err(format_err!("Failed to open out directory at {:?}: {}", path, e));
                }
            };

            self.populate_data_map(&inspect_proxy).await
        }
        .boxed()
    }
}

/// Packet containing a node hierarchy and all the metadata needed to
/// populate a diagnostics schema for that node hierarchy.
pub struct NodeHierarchyData {
    // Name of the file that created this snapshot.
    filename: String,
    // Timestamp at which this snapshot resolved or failed.
    timestamp: zx::Time,
    // Errors encountered when processing this snapshot.
    errors: Vec<formatter::Error>,
    // Optional NodeHierarchy of the inspect hierarchy, in case reading fails
    // and we have errors to share with client.
    hierarchy: Option<NodeHierarchy>,
}

impl Into<NodeHierarchyData> for SnapshotData {
    fn into(self: SnapshotData) -> NodeHierarchyData {
        match self.snapshot {
            Some(snapshot) => match convert_snapshot_to_node_hierarchy(snapshot) {
                Ok(node_hierarchy) => NodeHierarchyData {
                    filename: self.filename,
                    timestamp: self.timestamp,
                    errors: self.errors,
                    hierarchy: Some(node_hierarchy),
                },
                Err(e) => NodeHierarchyData {
                    filename: self.filename,
                    timestamp: self.timestamp,
                    errors: vec![formatter::Error { message: format!("{:?}", e) }],
                    hierarchy: None,
                },
            },
            None => NodeHierarchyData {
                filename: self.filename,
                timestamp: self.timestamp,
                errors: self.errors,
                hierarchy: None,
            },
        }
    }
}

/// Packet containing a snapshot and all the metadata needed to
/// populate a diagnostics schema for that snapshot.
pub struct SnapshotData {
    // Name of the file that created this snapshot.
    filename: String,
    // Timestamp at which this snapshot resolved or failed.
    timestamp: zx::Time,
    // Errors encountered when processing this snapshot.
    errors: Vec<formatter::Error>,
    // Optional snapshot of the inspect hierarchy, in case reading fails
    // and we have errors to share with client.
    snapshot: Option<ReadSnapshot>,
}

impl SnapshotData {
    // Constructs packet that timestamps and packages inspect snapshot for exfiltration.
    fn successful(snapshot: ReadSnapshot, filename: String) -> SnapshotData {
        SnapshotData {
            filename,
            timestamp: fasync::Time::now().into_zx(),
            errors: Vec::new(),
            snapshot: Some(snapshot),
        }
    }

    // Constructs packet that timestamps and packages inspect snapshot failure for exfiltration.
    fn failed(error: formatter::Error, filename: String) -> SnapshotData {
        SnapshotData {
            filename,
            timestamp: fasync::Time::now().into_zx(),
            errors: vec![error],
            snapshot: None,
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
    relative_moniker: Vec<String>,
    /// Vector of all the snapshots of inspect hierarchies under
    /// the diagnostics directory of the component identified by
    /// relative_moniker, along with the metadata needed to populate
    /// this snapshot's diagnostics schema.
    snapshots: Vec<SnapshotData>,
    /// Optional hierarchy matcher. If unset, the reader is running
    /// in all-access mode, meaning no matching or filtering is required.
    inspect_matcher: Option<InspectHierarchyMatcher>,
}

impl PopulatedInspectDataContainer {
    async fn try_from(
        unpopulated: UnpopulatedInspectDataContainer,
    ) -> Result<PopulatedInspectDataContainer, Error> {
        let mut collector = InspectDataCollector::new();

        match collector.populate_data_map(&unpopulated.component_diagnostics_proxy).await {
            Ok(_) => {
                let mut snapshots_data_opt = None;
                if let Some(data_map) = Box::new(collector).take_data() {
                    let mut acc: Vec<SnapshotData> = vec![];
                    for (filename, data) in data_map {
                        match data {
                            InspectData::Tree(tree, _) => match SnapshotTree::try_from(&tree).await {
                                Ok(snapshot_tree) => {
                                    acc.push(SnapshotData::successful(ReadSnapshot::Tree(snapshot_tree), filename));
                                }
                                Err(e) => {
                                    acc.push(SnapshotData::failed(formatter::Error{message: format!("{:?}", e)}, filename));
                                }
                            },
                            InspectData::DeprecatedFidl(inspect_proxy) => {
                                match deprecated_inspect::load_hierarchy(inspect_proxy)
                                    .on_timeout(
                                        INSPECT_ASYNC_TIMEOUT_SECONDS.seconds().after_now(),
                                        || {
                                            Err(format_err!(
                                                "Timed out reading via deprecated inspect protocol.",
                                            ))
                                        },
                                    )
                                    .await
                                {
                                    Ok(hierarchy) => {
                                        acc.push(SnapshotData::successful(ReadSnapshot::Finished(hierarchy), filename));
                                    }
                                    Err(e) => {
                                       acc.push(SnapshotData::failed(formatter::Error{message: format!("{:?}", e)}, filename));
                                   }
                                }
                            }
                            InspectData::Vmo(vmo) => match Snapshot::try_from(&vmo) {
                                Ok(snapshot) => {
                                    acc.push(SnapshotData::successful(ReadSnapshot::Single(snapshot), filename));
                                }
                                Err(e) => {
                                    acc.push(SnapshotData::failed(formatter::Error{message: format!("{:?}", e)}, filename));
                                }
                            },
                            InspectData::File(contents) => match Snapshot::try_from(contents) {
                                Ok(snapshot) => {
                                    acc.push(SnapshotData::successful(ReadSnapshot::Single(snapshot), filename));
                                }
                                Err(e) => {
                                    acc.push(SnapshotData::failed(formatter::Error{message: format!("{:?}", e)}, filename));
                                }
                            },
                            InspectData::Empty => {}
                        }
                    }
                    snapshots_data_opt = Some(acc);
                }
                match snapshots_data_opt {
                    Some(snapshots) => Ok(PopulatedInspectDataContainer {
                        relative_moniker: unpopulated.relative_moniker,
                        snapshots: snapshots,
                        inspect_matcher: unpopulated.inspect_matcher,
                    }),
                    None => Err(format_err!(
                        "Failed to parse snapshots for: {:?}.",
                        unpopulated.relative_moniker
                    )),
                }
            }
            Err(e) => Err(e),
        }
    }
}

/// UnpopulatedInspectDataContainer is the container that holds
/// all information needed to retrieve Inspect data
/// for a given component, when requested.
pub struct UnpopulatedInspectDataContainer {
    /// Relative moniker of the component that this data container
    /// is representing.
    relative_moniker: Vec<String>,
    /// DirectoryProxy for the out directory that this
    /// data packet is configured for.
    component_diagnostics_proxy: DirectoryProxy,
    /// Optional hierarchy matcher. If unset, the reader is running
    /// in all-access mode, meaning no matching or filtering is required.
    inspect_matcher: Option<InspectHierarchyMatcher>,
}

/// InspectDataRepository manages storage of all state needed in order
/// for the inspect reader to retrieve inspect data when a read is requested.
pub struct InspectDataRepository {
    // TODO(lukenicholson): Wrap directory proxies in a trie of
    // component names to make filtering by selectors work.
    data_directories: InspectDataTrie,
    /// Optional static selectors. For the all_access reader, there
    /// are no provided selectors. For all other pipelines, a non-empty
    /// vector is required.
    static_selectors: Option<Vec<Arc<Selector>>>,
}

impl InspectDataRepository {
    pub fn new(static_selectors: Option<Vec<Arc<Selector>>>) -> Self {
        InspectDataRepository {
            data_directories: InspectDataTrie::new(),
            static_selectors: static_selectors,
        }
    }

    pub fn remove(&mut self, component_id: &ComponentIdentifier) {
        self.data_directories.remove(component_id.unique_key());
    }

    pub fn add(
        &mut self,
        identifier: ComponentIdentifier,
        directory_proxy: DirectoryProxy,
    ) -> Result<(), Error> {
        let relative_moniker = identifier.relative_moniker_for_selectors();
        let matched_selectors = match &self.static_selectors {
            Some(selectors) => Some(selectors::match_component_moniker_against_selectors(
                &relative_moniker,
                &selectors,
            )?),
            None => None,
        };

        // The component events stream might contain duplicated events for out/diagnostics
        // directories of components that already existed before the archivist started or the
        // archivist itself, make sure we don't track duplicated component diagnostics directories.
        if self.contains(&identifier, &relative_moniker) {
            return Ok(());
        }

        let key = identifier.unique_key();
        match matched_selectors {
            Some(selectors) => {
                if !selectors.is_empty() {
                    self.data_directories.insert(
                        key,
                        UnpopulatedInspectDataContainer {
                            relative_moniker: relative_moniker,
                            component_diagnostics_proxy: directory_proxy,
                            inspect_matcher: Some((&selectors).try_into()?),
                        },
                    );
                }
                Ok(())
            }
            None => {
                self.data_directories.insert(
                    key,
                    UnpopulatedInspectDataContainer {
                        relative_moniker: relative_moniker,
                        component_diagnostics_proxy: directory_proxy,
                        inspect_matcher: None,
                    },
                );

                Ok(())
            }
        }
    }

    /// Return all of the DirectoryProxies that contain Inspect hierarchies
    /// which contain data that should be selected from.
    pub fn fetch_data(&self) -> Vec<UnpopulatedInspectDataContainer> {
        return self
            .data_directories
            .iter()
            .filter_map(
                |(_, unpopulated_data_container_opt)| match unpopulated_data_container_opt {
                    Some(unpopulated_data_container) => io_util::clone_directory(
                        &unpopulated_data_container.component_diagnostics_proxy,
                        CLONE_FLAG_SAME_RIGHTS,
                    )
                    .ok()
                    .map(|directory| UnpopulatedInspectDataContainer {
                        relative_moniker: unpopulated_data_container.relative_moniker.clone(),
                        component_diagnostics_proxy: directory,
                        inspect_matcher: unpopulated_data_container.inspect_matcher.clone(),
                    }),
                    None => None,
                },
            )
            .collect();
    }

    fn contains(
        &mut self,
        component_id: &ComponentIdentifier,
        relative_moniker: &[String],
    ) -> bool {
        self.data_directories
            .get(component_id.unique_key())
            .map(|trie_node| {
                trie_node
                    .get_values()
                    .iter()
                    .any(|container| container.relative_moniker == relative_moniker)
            })
            .unwrap_or(false)
    }
}

/// ReaderServer holds the state and data needed to serve Inspect data
/// reading requests for a single client.
///
/// configured_selectors: are the selectors provided by the client which define
///                       what inspect data is returned by read requests. A none type
///                       implies that all available data should be returned.
///
/// inspect_repo: the InspectDataRepository which holds the access-points for all relevant
///               inspect data.
#[derive(Clone)]
pub struct ReaderServer {
    pub inspect_repo: Arc<RwLock<InspectDataRepository>>,
    pub configured_selectors: Option<Vec<Arc<fidl_fuchsia_diagnostics::Selector>>>,
}

fn convert_snapshot_to_node_hierarchy(snapshot: ReadSnapshot) -> Result<NodeHierarchy, Error> {
    match snapshot {
        ReadSnapshot::Single(snapshot) => Ok(PartialNodeHierarchy::try_from(snapshot)?.into()),
        ReadSnapshot::Tree(snapshot_tree) => snapshot_tree.try_into(),
        ReadSnapshot::Finished(hierarchy) => Ok(hierarchy),
    }
}

impl ReaderServer {
    pub fn new(
        inspect_repo: Arc<RwLock<InspectDataRepository>>,
        configured_selectors: Option<Vec<fidl_fuchsia_diagnostics::Selector>>,
    ) -> Self {
        ReaderServer {
            inspect_repo,
            configured_selectors: configured_selectors.map(|selectors| {
                selectors.into_iter().map(|selector| Arc::new(selector)).collect()
            }),
        }
    }

    fn filter_single_components_snapshots(
        sanitized_moniker: String,
        snapshots: Vec<SnapshotData>,
        static_matcher: Option<InspectHierarchyMatcher>,
        client_matcher_container: &HashMap<String, Option<InspectHierarchyMatcher>>,
    ) -> Vec<NodeHierarchyData> {
        let statically_filtered_hierarchies: Vec<NodeHierarchyData> = match static_matcher {
            Some(static_matcher) => snapshots
                .into_iter()
                .map(|snapshot_data| {
                    let node_hierarchy_data: NodeHierarchyData = snapshot_data.into();

                    match node_hierarchy_data.hierarchy {
                        Some(node_hierarchy) => {
                            match fuchsia_inspect_node_hierarchy::filter_node_hierarchy(
                                node_hierarchy,
                                &static_matcher,
                            ) {
                                Ok(filtered_hierarchy_opt) => NodeHierarchyData {
                                    filename: node_hierarchy_data.filename,
                                    timestamp: node_hierarchy_data.timestamp,
                                    errors: node_hierarchy_data.errors,
                                    hierarchy: filtered_hierarchy_opt,
                                },
                                Err(e) => {
                                    error!("Archivist failed to filter a node hierarchy: {:?}", e);
                                    NodeHierarchyData {
                                        filename: node_hierarchy_data.filename,
                                        timestamp: node_hierarchy_data.timestamp,
                                        errors: vec![formatter::Error {
                                            message: format!("{:?}", e),
                                        }],
                                        hierarchy: None,
                                    }
                                }
                            }
                        }
                        None => NodeHierarchyData {
                            filename: node_hierarchy_data.filename,
                            timestamp: node_hierarchy_data.timestamp,
                            errors: node_hierarchy_data.errors,
                            hierarchy: None,
                        },
                    }
                })
                .collect(),

            // The only way we have a None value for the PopulatedDataContainer is
            // if there were no provided static selectors, which is only valid in
            // the AllAccess pipeline. For all other pipelines, if no static selectors
            // matched, the data wouldn't have ended up in the repository to begin
            // with.
            None => snapshots.into_iter().map(|snapshot_data| snapshot_data.into()).collect(),
        };

        match client_matcher_container.get(&sanitized_moniker) {
            // If the moniker key was present, and there was an InspectHierarchyMatcher,
            // then this means the client provided their own selectors, and a subset of
            // them matched this component. So we need to filter each of the snapshots from
            // this component with the dynamically provided components.
            Some(Some(dynamic_matcher)) => statically_filtered_hierarchies
                .into_iter()
                .map(|node_hierarchy_data| match node_hierarchy_data.hierarchy {
                    Some(node_hierarchy) => {
                        match fuchsia_inspect_node_hierarchy::filter_node_hierarchy(
                            node_hierarchy,
                            &dynamic_matcher,
                        ) {
                            Ok(filtered_hierarchy_opt) => NodeHierarchyData {
                                filename: node_hierarchy_data.filename,
                                timestamp: node_hierarchy_data.timestamp,
                                errors: node_hierarchy_data.errors,
                                hierarchy: filtered_hierarchy_opt,
                            },
                            Err(e) => {
                                eprintln!("Archivist failed to filter a node hierarchy: {:?}", e);
                                NodeHierarchyData {
                                    filename: node_hierarchy_data.filename,
                                    timestamp: node_hierarchy_data.timestamp,
                                    errors: vec![formatter::Error { message: format!("{:?}", e) }],
                                    hierarchy: None,
                                }
                            }
                        }
                    }
                    None => NodeHierarchyData {
                        filename: node_hierarchy_data.filename,
                        timestamp: node_hierarchy_data.timestamp,
                        errors: node_hierarchy_data.errors,
                        hierarchy: None,
                    },
                })
                .collect(),
            // If the moniker key was present, but the InspectHierarchyMatcher option was
            // None, this means that the client provided their own selectors, and none of
            // them matched this particular component, so no values are to be returned.
            Some(None) => Vec::new(),
            // If the moniker key was absent, then the entire client_matcher_container should
            // be empty since the implication is that the client provided none of their own
            // selectors. Either every moniker is present or none are. And, if no dynamically
            // provided selectors exist, then the statically filtered snapshots are all that
            // we need.
            None => {
                assert!(client_matcher_container.is_empty());
                statically_filtered_hierarchies
            }
        }
    }

    /// Takes a batch of unpopulated inspect data containers, traverses their diagnostics
    /// directories, takes snapshots of all the Inspect hierarchies in those directories,
    /// and then transforms the data containers into `PopulatedInspectDataContainer` results.
    ///
    /// An entry is only an Error if connecting to the directory fails. Within a component's
    /// diagnostics directory, individual snapshots of hierarchies can fail and the transformation
    /// to a PopulatedInspectDataContainer will still succeed.
    async fn pump_inspect_data(
        inspect_batch: Vec<UnpopulatedInspectDataContainer>,
    ) -> Vec<Result<PopulatedInspectDataContainer, Error>> {
        join_all(inspect_batch.into_iter().map(move |inspect_data_packet| {
            PopulatedInspectDataContainer::try_from(inspect_data_packet)
        }))
        .await
    }

    /// Takes a batch of PopulatedInspectDataContainer results, and for all the non-error
    /// entries converts all snapshots into in-memory node hierarchies, filters those hierarchies
    /// so that the only diagnostics properties they contain are those configured by the static
    /// and client-provided selectors, and then packages the filtered hierarchies into
    /// HierarchyData data structs.
    ///
    // TODO(4601): Error entries should still be included, but with a custom hierarchy
    //             that makes it clear to clients that snapshotting failed.
    pub fn filter_snapshots(
        configured_selectors: &Option<Vec<Arc<Selector>>>,
        pumped_inspect_data_results: Vec<Result<PopulatedInspectDataContainer, Error>>,
    ) -> Vec<(Moniker, NodeHierarchyData)> {
        // In case we encounter multiple PopulatedDataContainers with the same moniker we don't
        // want to do the component selector filtering again, so store the results in a map.
        let mut client_selector_matches: HashMap<String, Option<InspectHierarchyMatcher>> =
            HashMap::new();

        // We iterate the vector of pumped inspect data packets, consuming each inspect vmo
        // and filtering it using the provided selector regular expressions. Each filtered
        // inspect hierarchy is then added to an accumulator as a HierarchyData to be converted
        // into a JSON string and returned.
        pumped_inspect_data_results.into_iter().fold(Vec::new(), |mut acc, pumped_data| {
            match pumped_data {
                Ok(PopulatedInspectDataContainer {
                    relative_moniker,
                    snapshots,
                    inspect_matcher,
                }) => {
                    let sanitized_moniker = relative_moniker
                        .iter()
                        .map(|s| selectors::sanitize_string_for_selectors(s))
                        .collect::<Vec<String>>()
                        .join("/");

                    // We know that if configured_selectors is some, there is atleast one entry
                    // since the server validates the stream parameters and an empty
                    // configured_selectors vector is an error.
                    if configured_selectors.is_some() {
                        let configured_matchers = client_selector_matches
                            .entry(sanitized_moniker.clone())
                            .or_insert_with(|| {
                                let matching_selectors =
                                    selectors::match_component_moniker_against_selectors(
                                        &relative_moniker,
                                        // Safe unwrap since we verify it is Some above.
                                        configured_selectors.as_ref().unwrap(),
                                    )
                                    .unwrap_or_else(|err| {
                                        error!(
                                        "Failed to evaluate client selectors for: {:?} Error: {:?}",
                                        relative_moniker, err
                                    );
                                        Vec::new()
                                    });

                                if matching_selectors.is_empty() {
                                    None
                                } else {
                                    match (&matching_selectors).try_into() {
                                        Ok(hierarchy_matcher) => Some(hierarchy_matcher),
                                        Err(e) => {
                                            error!("Failed to create hierarchy matcher: {:?}", e);
                                            None
                                        }
                                    }
                                }
                            });

                        // If there were configured matchers and none of them matched
                        // this component, then we should return early since there is no data to
                        // extract.
                        if configured_matchers.is_none() {
                            return acc;
                        }
                    };

                    let mut filtered_hierarchy_data_with_moniker: Vec<(String, NodeHierarchyData)> =
                        ReaderServer::filter_single_components_snapshots(
                            sanitized_moniker.clone(),
                            snapshots,
                            inspect_matcher,
                            &client_selector_matches,
                        )
                        .into_iter()
                        .map(|filtered_hierarchy_data| {
                            (sanitized_moniker.clone(), filtered_hierarchy_data)
                        })
                        .collect();

                    acc.append(&mut filtered_hierarchy_data_with_moniker);
                    acc
                }
                // TODO(36761): What does it mean for IO to fail on a
                // subset of directory data collections?
                Err(_) => acc,
            }
        })
    }

    /// Takes a vector of HierarchyData structs, and a `fidl_fuchsia_diagnostics/Format`
    /// enum, and writes each diagnostics hierarchy into a READ_ONLY VMO according to
    /// provided format. This VMO is then packaged into a `fidl_fuchsia_mem/Buffer`
    /// which is then packaged into a `fidl_fuchsia_diagnostics/FormattedContent`
    /// xunion which specifies the format of the VMO for clients.
    ///
    /// Errors in the returned Vector correspond to IO failures in writing to a VMO. If
    /// a node hierarchy fails to format, its vmo is an empty string.
    fn format_hierarchies(
        format: &fidl_fuchsia_diagnostics::Format,
        hierarchies_with_monikers: Vec<(Moniker, NodeHierarchyData)>,
    ) -> Vec<Result<fidl_fuchsia_diagnostics::FormattedContent, Error>> {
        hierarchies_with_monikers
            .into_iter()
            .map(|(moniker, hierarchy_data)| {
                let formatted_string_result = match format {
                    fidl_fuchsia_diagnostics::Format::Json => {
                        let inspect_schema = JsonInspectSchema::new(
                            moniker,
                            hierarchy_data.hierarchy,
                            hierarchy_data.timestamp,
                            hierarchy_data.filename,
                            hierarchy_data.errors,
                        );

                        Ok(serde_json::to_string_pretty(&inspect_schema)?)
                    }
                    fidl_fuchsia_diagnostics::Format::Text => {
                        Err(format_err!("Text formatting not supported for inspect."))
                    }
                };

                let content_string = match formatted_string_result {
                    Ok(formatted_string) => formatted_string,
                    Err(e) => {
                        // TODO(4601): Convert failed formattings into the
                        // canonical json schema, with a failure message in "data"
                        error!("parsing results from the inspect source failed: {:?}", e);
                        "".to_string()
                    }
                };

                let vmo_size: u64 = content_string.len() as u64;

                let dump_vmo_result: Result<zx::Vmo, Error> = zx::Vmo::create(vmo_size as u64)
                    .map_err(|s| format_err!("error creating buffer, zx status: {}", s));

                dump_vmo_result.and_then(|dump_vmo| {
                    dump_vmo
                        .write(content_string.as_bytes(), 0)
                        .map_err(|s| format_err!("error writing buffer, zx status: {}", s))?;

                    let client_vmo =
                        dump_vmo.duplicate_handle(zx::Rights::READ | zx::Rights::BASIC)?;

                    let mem_buffer = fidl_fuchsia_mem::Buffer { vmo: client_vmo, size: vmo_size };

                    match format {
                        fidl_fuchsia_diagnostics::Format::Json => {
                            Ok(fidl_fuchsia_diagnostics::FormattedContent::Json(mem_buffer))
                        }
                        fidl_fuchsia_diagnostics::Format::Text => {
                            Ok(fidl_fuchsia_diagnostics::FormattedContent::Json(mem_buffer))
                        }
                    }
                })
            })
            .collect()
    }

    /// Takes a BatchIterator server channel and upon receiving a GetNext request, serves
    /// an empty vector denoting that the iterator has reached its end and is terminating.
    pub async fn serve_terminal_batch(
        &self,
        stream: &mut BatchIteratorRequestStream,
    ) -> Result<(), Error> {
        if stream.is_terminated() {
            return Ok(());
        }

        while let Some(req) = stream.try_next().await? {
            match req {
                fidl_fuchsia_diagnostics::BatchIteratorRequest::GetNext { responder } => {
                    responder.send(&mut Ok(Vec::new()))?;
                }
            }
            break;
        }
        Ok(())
    }

    /// Takes a BatchIterator server channel and starts serving snapshotted
    /// Inspect hierarchies to clients as vectors of FormattedContent. The hierarchies
    /// are served in batches of `IN_MEMORY_SNAPSHOT_LIMIT` at a time, and snapshots of
    /// diagnostics data aren't taken until a component is included in the upcoming batch.
    ///
    /// NOTE: This API does not send the terminal empty-vector at the end of the snapshot.
    pub async fn serve_inspect_snapshot(
        &self,
        stream: &mut BatchIteratorRequestStream,
        format: &fidl_fuchsia_diagnostics::Format,
    ) -> Result<(), Error> {
        if stream.is_terminated() {
            return Ok(());
        }

        // We must fetch the repositories in a closure to prevent the
        // repository mutex-guard from leaking into futures.
        let inspect_repo_data = self.inspect_repo.read().fetch_data();

        let inspect_repo_length = inspect_repo_data.len();
        let mut inspect_repo_iter = inspect_repo_data.into_iter();
        let mut iter = 0;
        let max = (inspect_repo_length - 1 / IN_MEMORY_SNAPSHOT_LIMIT) + 1;
        while let Some(req) = stream.try_next().await? {
            match req {
                fidl_fuchsia_diagnostics::BatchIteratorRequest::GetNext { responder } => {
                    let snapshot_batch: Vec<UnpopulatedInspectDataContainer> =
                        (&mut inspect_repo_iter).take(IN_MEMORY_SNAPSHOT_LIMIT).collect();

                    iter = iter + 1;

                    // Asynchronously populate data containers with snapshots of relevant
                    // inspect hierarchies.
                    let pumped_inspect_data_results =
                        ReaderServer::pump_inspect_data(snapshot_batch).await;

                    // Apply selector filtering to all snapshot inspect hierarchies in the batch
                    let batch_hierarchy_data = ReaderServer::filter_snapshots(
                        &self.configured_selectors,
                        pumped_inspect_data_results,
                    );

                    let formatted_content: Vec<
                        Result<fidl_fuchsia_diagnostics::FormattedContent, Error>,
                    > = ReaderServer::format_hierarchies(format, batch_hierarchy_data);

                    let filtered_results: Vec<fidl_fuchsia_diagnostics::FormattedContent> =
                        formatted_content.into_iter().filter_map(Result::ok).collect();
                    responder.send(&mut Ok(filtered_results))?;
                }
            }

            // We've sent all the meaningful content available in snapshot mode.
            // The terminal value must be handled separately.
            if iter == max - 1 {
                break;
            }
        }
        Ok(())
    }

    pub fn stream_inspect(
        self,
        stream_mode: fidl_fuchsia_diagnostics::StreamMode,
        format: fidl_fuchsia_diagnostics::Format,
        result_stream: ServerEnd<BatchIteratorMarker>,
    ) -> Result<(), Error> {
        let result_channel = fasync::Channel::from_channel(result_stream.into_channel())?;

        fasync::spawn(
            async move {
                let mut iterator_req_stream =
                    fidl_fuchsia_diagnostics::BatchIteratorRequestStream::from_channel(
                        result_channel,
                    );

                if stream_mode == fidl_fuchsia_diagnostics::StreamMode::Snapshot
                    || stream_mode == fidl_fuchsia_diagnostics::StreamMode::SnapshotThenSubscribe
                {
                    self.serve_inspect_snapshot(&mut iterator_req_stream, &format).await?;
                }

                if stream_mode == fidl_fuchsia_diagnostics::StreamMode::Subscribe
                    || stream_mode == fidl_fuchsia_diagnostics::StreamMode::SnapshotThenSubscribe
                {
                    error!("not yet supported");
                }

                self.serve_terminal_batch(&mut iterator_req_stream).await?;

                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| {
                error!("Error encountered running inspect stream: {:?}", e);
            }),
        );

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::events::types::{LegacyIdentifier, RealmPath},
        fdio,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_io::DirectoryMarker,
        fuchsia_async as fasync,
        fuchsia_component::server::ServiceFs,
        fuchsia_inspect::{assert_inspect_tree, reader, Inspector},
        fuchsia_zircon as zx,
        fuchsia_zircon::Peered,
        futures::StreamExt,
        serde_json::json,
    };

    fn get_vmo(text: &[u8]) -> zx::Vmo {
        let vmo = zx::Vmo::create(4096).unwrap();
        vmo.write(text, 0).unwrap();
        vmo
    }

    #[fasync::run_singlethreaded(test)]
    async fn inspect_data_collector() {
        let path = PathBuf::from("/test-bindings");
        // Make a ServiceFs containing two files.
        // One is an inspect file, and one is not.
        let mut fs = ServiceFs::new();
        let vmo = get_vmo(b"test1");
        let vmo2 = get_vmo(b"test2");
        let vmo3 = get_vmo(b"test3");
        let vmo4 = get_vmo(b"test4");
        fs.dir("diagnostics").add_vmo_file_at("root.inspect", vmo, 0, 4096);
        fs.dir("diagnostics").add_vmo_file_at("root_not_inspect", vmo2, 0, 4096);
        fs.dir("diagnostics").dir("a").add_vmo_file_at("root.inspect", vmo3, 0, 4096);
        fs.dir("diagnostics").dir("b").add_vmo_file_at("root.inspect", vmo4, 0, 4096);
        // Create a connection to the ServiceFs.
        let (h0, h1) = zx::Channel::create().unwrap();
        fs.serve_connection(h1).unwrap();

        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.join("out").to_str().unwrap(), h0).unwrap();

        fasync::spawn(fs.collect());

        let (done0, done1) = zx::Channel::create().unwrap();

        let thread_path = path.join("out/diagnostics");

        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let path = thread_path;
            let done = done1;
            let mut executor = fasync::Executor::new().unwrap();

            executor.run_singlethreaded(async {
                let collector = InspectDataCollector::new();
                // Trigger collection on a clone of the inspect collector so
                // we can use collector to take the collected data.
                Box::new(collector.clone()).collect(path).await.unwrap();
                let collector: Box<InspectDataCollector> = Box::new(collector);

                let extra_data = collector.take_data().expect("collector missing data");
                assert_eq!(3, extra_data.len());

                let assert_extra_data = |path: &str, content: &[u8]| {
                    let extra = extra_data.get(path);
                    assert!(extra.is_some());

                    match extra.unwrap() {
                        InspectData::Vmo(vmo) => {
                            let mut buf = [0u8; 5];
                            vmo.read(&mut buf, 0).expect("reading vmo");
                            assert_eq!(content, &buf);
                        }
                        v => {
                            panic!("Expected Vmo, got {:?}", v);
                        }
                    }
                };

                assert_extra_data("root.inspect", b"test1");
                assert_extra_data("a/root.inspect", b"test3");
                assert_extra_data("b/root.inspect", b"test4");

                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn inspect_data_collector_tree() {
        let path = PathBuf::from("/test-bindings2");

        // Make a ServiceFs serving an inspect tree.
        let mut fs = ServiceFs::new();
        let inspector = Inspector::new();
        inspector.root().record_int("a", 1);
        inspector.root().record_lazy_child("lazy", || {
            async move {
                let inspector = Inspector::new();
                inspector.root().record_double("b", 3.14);
                Ok(inspector)
            }
            .boxed()
        });
        inspector.serve(&mut fs).expect("failed to serve inspector");

        // Create a connection to the ServiceFs.
        let (h0, h1) = zx::Channel::create().unwrap();
        fs.serve_connection(h1).unwrap();

        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.join("out").to_str().unwrap(), h0).unwrap();

        fasync::spawn(fs.collect());

        let (done0, done1) = zx::Channel::create().unwrap();
        let thread_path = path.join("out/diagnostics");

        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let path = thread_path;
            let done = done1;
            let mut executor = fasync::Executor::new().unwrap();

            executor.run_singlethreaded(async {
                let collector = InspectDataCollector::new();

                //// Trigger collection on a clone of the inspect collector so
                //// we can use collector to take the collected data.
                Box::new(collector.clone()).collect(path).await.unwrap();
                let collector: Box<InspectDataCollector> = Box::new(collector);

                let extra_data = collector.take_data().expect("collector missing data");
                assert_eq!(1, extra_data.len());

                let extra = extra_data.get(TreeMarker::SERVICE_NAME);
                assert!(extra.is_some());

                match extra.unwrap() {
                    InspectData::Tree(tree, vmo) => {
                        // Assert we can read the tree proxy and get the data we expected.
                        let hierarchy = reader::read_from_tree(tree)
                            .await
                            .expect("failed to read hierarchy from tree");
                        assert_inspect_tree!(hierarchy, root: {
                            a: 1i64,
                            lazy: {
                                b: 3.14,
                            }
                        });
                        let partial_hierarchy: NodeHierarchy =
                            PartialNodeHierarchy::try_from(vmo.as_ref().unwrap())
                                .expect("failed to read hierarchy from vmo")
                                .into();
                        // Assert the vmo also points to that data (in this case since there's no
                        // lazy nodes).
                        assert_inspect_tree!(partial_hierarchy, root: {
                            a: 1i64,
                        });
                    }
                    v => {
                        panic!("Expected Tree, got {:?}", v);
                    }
                }

                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn reader_server_formatting() {
        let path = PathBuf::from("/test-bindings3");

        // Make a ServiceFs containing two files.
        // One is an inspect file, and one is not.
        let mut fs = ServiceFs::new();
        let vmo = zx::Vmo::create(4096).unwrap();
        let inspector = inspector_for_reader_test();

        let data = inspector.copy_vmo_data().unwrap();
        vmo.write(&data, 0).unwrap();
        fs.dir("diagnostics").add_vmo_file_at("test.inspect", vmo, 0, 4096);

        // Create a connection to the ServiceFs.
        let (h0, h1) = zx::Channel::create().unwrap();
        fs.serve_connection(h1).unwrap();

        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.join("out").to_str().unwrap(), h0).unwrap();

        fasync::spawn(fs.collect());
        let (done0, done1) = zx::Channel::create().unwrap();
        let thread_path = path.join("out");

        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let path = thread_path;
            let done = done1;
            let mut executor = fasync::Executor::new().unwrap();
            executor.run_singlethreaded(async {
                verify_reader(path).await;
                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_server_formatting_tree() {
        let path = PathBuf::from("/test-bindings4");

        // Make a ServiceFs containing two files.
        // One is an inspect file, and one is not.
        let mut fs = ServiceFs::new();
        let inspector = inspector_for_reader_test();
        inspector.serve(&mut fs).expect("failed to serve inspector");

        // Create a connection to the ServiceFs.
        let (h0, h1) = zx::Channel::create().unwrap();
        fs.serve_connection(h1).unwrap();

        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.join("out").to_str().unwrap(), h0).unwrap();

        fasync::spawn(fs.collect());
        let (done0, done1) = zx::Channel::create().unwrap();
        let thread_path = path.join("out");

        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let path = thread_path;
            let done = done1;
            let mut executor = fasync::Executor::new().unwrap();
            executor.run_singlethreaded(async {
                verify_reader(path).await;
                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });
        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn inspect_repo_disallows_duplicated_dirs() {
        let mut inspect_repo = InspectDataRepository::new(None);
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });
        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");
        inspect_repo.add(component_id.clone(), proxy).expect("add to repo");

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");
        inspect_repo.add(component_id.clone(), proxy).expect("add to repo");

        let key = component_id.unique_key();
        assert_eq!(inspect_repo.data_directories.get(key).unwrap().get_values().len(), 1);
    }

    fn inspector_for_reader_test() -> Inspector {
        let inspector = Inspector::new();
        let root = inspector.root();
        let child_1 = root.create_child("child_1");
        child_1.record_int("some-int", 2);
        let child_1_1 = child_1.create_child("child_1_1");
        child_1_1.record_int("some-int", 3);
        child_1_1.record_int("not-wanted-int", 4);
        root.record(child_1_1);
        root.record(child_1);
        let child_2 = root.create_child("child_2");
        child_2.record_int("some-int", 2);
        root.record(child_2);
        inspector
    }

    async fn verify_reader(path: PathBuf) {
        let child_1_1_selector = selectors::parse_selector(r#"*:root/child_1/*:some-int"#).unwrap();
        let child_2_selector =
            selectors::parse_selector(r#"test_component.cmx:root/child_2:*"#).unwrap();
        let inspect_repo = Arc::new(RwLock::new(InspectDataRepository::new(Some(vec![
            Arc::new(child_1_1_selector),
            Arc::new(child_2_selector),
        ]))));

        let out_dir_proxy = InspectDataCollector::find_directory_proxy(&path).await.unwrap();

        // The absolute moniker here is made up since the selector is a glob
        // selector, so any path would match.
        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id: "1234".into(),
            realm_path: vec![].into(),
            component_name: "test_component.cmx".into(),
        });
        inspect_repo.write().add(component_id.clone(), out_dir_proxy).unwrap();

        let reader_server = ReaderServer::new(inspect_repo.clone(), None);

        let result_json = read_snapshot(reader_server.clone()).await;

        let result_array = result_json.as_array().expect("unit test json should be array.");
        assert_eq!(result_array.len(), 1, "Expect only one schema to be returned.");

        let result_map =
            result_array[0].as_object().expect("entries in the schema array are json objects.");

        let result_payload =
            result_map.get("payload").expect("diagnostics schema requires payload entry.");

        let expected_payload = json!({
            "root": {
                "child_1": {
                    "child_1_1": {
                        "some-int": 3
                    }
                },
                "child_2": {
                    "some-int": 2
                }
            }
        });
        assert_eq!(*result_payload, expected_payload);

        inspect_repo.write().remove(&component_id);
        let result_json = read_snapshot(reader_server.clone()).await;

        let result_array = result_json.as_array().expect("unit test json should be array.");
        assert_eq!(result_array.len(), 0, "Expect no schemas to be returned.");
    }

    async fn read_snapshot(reader_server: ReaderServer) -> serde_json::Value {
        let (consumer, batch_iterator): (
            _,
            ServerEnd<fidl_fuchsia_diagnostics::BatchIteratorMarker>,
        ) = create_proxy().unwrap();

        fasync::spawn(async move {
            reader_server
                .stream_inspect(
                    fidl_fuchsia_diagnostics::StreamMode::Snapshot,
                    fidl_fuchsia_diagnostics::Format::Json,
                    batch_iterator,
                )
                .unwrap();
        });

        let mut result_vec: Vec<String> = Vec::new();
        loop {
            let next_batch: Vec<fidl_fuchsia_diagnostics::FormattedContent> =
                consumer.get_next().await.unwrap().unwrap();
            if next_batch.is_empty() {
                break;
            }
            for formatted_content in next_batch {
                match formatted_content {
                    fidl_fuchsia_diagnostics::FormattedContent::Json(data) => {
                        let mut buf = vec![0; data.size as usize];
                        data.vmo.read(&mut buf, 0).expect("reading vmo");
                        let hierarchy_string = std::str::from_utf8(&buf).unwrap();
                        result_vec.push(hierarchy_string.to_string());
                    }
                    _ => panic!("test only produces json formatted data"),
                }
            }
        }
        let result_string = format!("[{}]", result_vec.join(","));
        serde_json::from_str(&result_string)
            .expect(&format!("unit tests shouldn't be creating malformed json: {}", result_string))
    }
}
