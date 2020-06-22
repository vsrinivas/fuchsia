// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        constants::INSPECT_ASYNC_TIMEOUT_SECONDS, diagnostics::DiagnosticsServerStats,
        events::types::InspectData,
    },
    anyhow::{format_err, Error},
    async_trait::async_trait,
    diagnostics_schema::{self as schema, LifecycleType},
    fidl::endpoints::DiscoverableService,
    fidl::endpoints::{RequestStream, ServerEnd},
    fidl_fuchsia_diagnostics::{self, BatchIteratorMarker, BatchIteratorRequestStream},
    fidl_fuchsia_inspect::TreeMarker,
    fidl_fuchsia_inspect_deprecated::InspectMarker,
    fidl_fuchsia_io::{DirectoryProxy, NodeInfo},
    files_async,
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_inspect::reader::snapshot::{Snapshot, SnapshotTree},
    fuchsia_inspect::NumericProperty,
    fuchsia_inspect_node_hierarchy::{InspectHierarchyMatcher, NodeHierarchy, Property},
    fuchsia_zircon::{self as zx, DurationNum},
    futures::future::BoxFuture,
    futures::stream::FusedStream,
    futures::stream::StreamExt,
    futures::{FutureExt, TryFutureExt, TryStreamExt},
    inspect_fidl_load as deprecated_inspect, io_util,
    log::error,
    parking_lot::Mutex,
    pin_utils::pin_mut,
    std::collections::HashMap,
    std::convert::TryFrom,
    std::path::{Path, PathBuf},
    std::sync::Arc,
};

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

pub enum ReadSnapshot {
    Single(Snapshot),
    Tree(SnapshotTree),
    Finished(NodeHierarchy),
}

/// Mapping from a diagnostics filename to the underlying encoding of that
/// diagnostics data.
pub type DataMap = HashMap<String, InspectData>;

pub type Moniker = String;

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
            if let Ok(Some(proxy)) = self.maybe_load_service::<TreeMarker>(inspect_proxy, &entry) {
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

            if let Ok(Some(proxy)) = self.maybe_load_service::<InspectMarker>(inspect_proxy, &entry)
            {
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
            let (proxy, server) = fidl::endpoints::create_proxy::<S>()?;
            fdio::service_connect_at(dir_proxy.as_ref(), &entry.name, server.into_channel())?;
            return Ok(Some(proxy));
        }
        Ok(None)
    }

    /// Takes the contained extra data. Additions following this have no effect.
    pub fn take_data(self: Box<Self>) -> Option<DataMap> {
        self.inspect_data_map.lock().take()
    }

    /// Collect extra data stored under the given path.
    ///
    /// This currently only does a single pass over the directory to find information.
    pub fn collect(mut self: Box<Self>, path: PathBuf) -> BoxFuture<'static, Result<(), Error>> {
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
                            InspectData::Tree(tree, _) => match SnapshotTree::try_from(&tree).await {
                                Ok(snapshot_tree) => {
                                    acc.push(SnapshotData::successful(
                                        ReadSnapshot::Tree(snapshot_tree), filename));
                                }
                                Err(e) => {
                                    acc.push(SnapshotData::failed(
                                        schema::Error{message: format!("{:?}", e)}, filename));
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
                                        acc.push(SnapshotData::successful(
                                            ReadSnapshot::Finished(hierarchy), filename));
                                    }
                                    Err(e) => {
                                        acc.push(SnapshotData::failed(
                                            schema::Error{message: format!("{:?}", e)}, filename));
                                   }
                                }
                            }
                            InspectData::Vmo(vmo) => match Snapshot::try_from(&vmo) {
                                Ok(snapshot) => {
                                    acc.push(SnapshotData::successful(
                                        ReadSnapshot::Single(snapshot), filename));
                                }
                                Err(e) => {
                                    acc.push(SnapshotData::failed(
                                        schema::Error{message: format!("{:?}", e)}, filename));
                                }
                            },
                            InspectData::File(contents) => match Snapshot::try_from(contents) {
                                Ok(snapshot) => {
                                    acc.push(SnapshotData::successful(
                                        ReadSnapshot::Single(snapshot), filename));
                                }
                                Err(e) => {
                                    acc.push(SnapshotData::failed(
                                        schema::Error{message: format!("{:?}", e)}, filename));
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

#[async_trait]
pub trait DiagnosticsServer: 'static + Sized + Send + Sync {
    /// Serve a snapshot of the buffered diagnostics data on system.
    async fn serve_snapshot(
        &self,
        stream: &mut BatchIteratorRequestStream,
        format: &fidl_fuchsia_diagnostics::Format,
        diagnostics_server_stats: Arc<DiagnosticsServerStats>,
    ) -> Result<(), Error>;

    /// Serve an empty vector to the client. The terminal vector will be sent
    /// until the client closes their connection.
    async fn serve_terminal_batch(
        &self,
        stream: &mut BatchIteratorRequestStream,
        diagnostics_server_stats: Arc<DiagnosticsServerStats>,
    ) -> Result<(), Error> {
        if stream.is_terminated() {
            return Ok(());
        }
        while let Some(req) = stream.try_next().await? {
            match req {
                fidl_fuchsia_diagnostics::BatchIteratorRequest::GetNext { responder } => {
                    diagnostics_server_stats.global_stats.batch_iterator_get_next_requests.add(1);
                    diagnostics_server_stats.global_stats.batch_iterator_get_next_responses.add(1);
                    diagnostics_server_stats.batch_iterator_get_next_requests.add(1);
                    diagnostics_server_stats.batch_iterator_get_next_responses.add(1);
                    diagnostics_server_stats.batch_iterator_terminal_responses.add(1);
                    responder.send(&mut Ok(Vec::new()))?;
                }
            }
        }
        Ok(())
    }

    fn stream_diagnostics(
        self,
        stream_mode: fidl_fuchsia_diagnostics::StreamMode,
        format: fidl_fuchsia_diagnostics::Format,
        result_stream: ServerEnd<BatchIteratorMarker>,
        server_stats: Arc<DiagnosticsServerStats>,
    ) -> Result<(), Error> {
        let result_channel = fasync::Channel::from_channel(result_stream.into_channel())?;
        let errorful_server_stats = server_stats.clone();

        fasync::spawn(
            async move {
                server_stats.global_stats.batch_iterator_connections_opened.add(1);

                let mut iterator_req_stream =
                    fidl_fuchsia_diagnostics::BatchIteratorRequestStream::from_channel(
                        result_channel,
                    );

                if stream_mode == fidl_fuchsia_diagnostics::StreamMode::Snapshot
                    || stream_mode == fidl_fuchsia_diagnostics::StreamMode::SnapshotThenSubscribe
                {
                    self.serve_snapshot(&mut iterator_req_stream, &format, server_stats.clone())
                        .await?;
                }

                if stream_mode == fidl_fuchsia_diagnostics::StreamMode::Subscribe
                    || stream_mode == fidl_fuchsia_diagnostics::StreamMode::SnapshotThenSubscribe
                {
                    error!("not yet supported");
                }
                self.serve_terminal_batch(&mut iterator_req_stream, server_stats.clone()).await?;
                server_stats.global_stats.batch_iterator_connections_closed.add(1);
                Ok(())
            }
            .unwrap_or_else(move |e: anyhow::Error| {
                errorful_server_stats.global_stats.batch_iterator_get_next_errors.add(1);
                errorful_server_stats.global_stats.batch_iterator_connections_closed.add(1);
                error!("Error encountered running diagnostics server: {:?}", e);
            }),
        );
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::events::types::InspectData,
        fdio,
        fidl::endpoints::DiscoverableService,
        fidl_fuchsia_inspect::TreeMarker,
        fuchsia_async as fasync,
        fuchsia_component::server::ServiceFs,
        fuchsia_inspect::reader::PartialNodeHierarchy,
        fuchsia_inspect::{assert_inspect_tree, reader, Inspector},
        fuchsia_inspect_node_hierarchy::NodeHierarchy,
        fuchsia_zircon as zx,
        fuchsia_zircon::Peered,
        futures::{FutureExt, StreamExt},
        std::path::PathBuf,
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
                        let hierarchy = reader::read_from_tree(&tree)
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
}
