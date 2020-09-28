// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        constants,
        container::{
            PopulatedInspectDataContainer, ReadSnapshot, SnapshotData,
            UnpopulatedInspectDataContainer,
        },
        diagnostics::{self, DiagnosticsServerStats},
        formatter,
        repository::DiagnosticsDataRepository,
        server::DiagnosticsServer,
    },
    anyhow::Error,
    async_trait::async_trait,
    collector::Moniker,
    diagnostics_data::{self as schema, Data},
    fidl_fuchsia_diagnostics::{self, BatchIteratorRequestStream, Format, StreamMode},
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_inspect::reader::PartialNodeHierarchy,
    fuchsia_inspect_node_hierarchy::{InspectHierarchyMatcher, NodeHierarchy},
    fuchsia_zircon::{self as zx, DurationNum},
    futures::channel::mpsc::{channel, Receiver},
    futures::sink::SinkExt,
    futures::stream,
    futures::stream::FusedStream,
    futures::stream::StreamExt,
    futures::TryStreamExt,
    log::error,
    parking_lot::RwLock,
    selectors,
    std::convert::{TryFrom, TryInto},
    std::sync::Arc,
};

pub mod collector;

/// Packet containing a node hierarchy and all the metadata needed to
/// populate a diagnostics schema for that node hierarchy.
pub struct NodeHierarchyData {
    // Name of the file that created this snapshot.
    filename: String,
    // Timestamp at which this snapshot resolved or failed.
    timestamp: zx::Time,
    // Errors encountered when processing this snapshot.
    errors: Vec<schema::Error>,
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
                    errors: vec![schema::Error { message: format!("{:?}", e) }],
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

/// ReaderServer holds the state and data needed to serve Inspect data
/// reading requests for a single client.
///
/// configured_selectors: are the selectors provided by the client which define
///                       what inspect data is returned by read requests. A none type
///                       implies that all available data should be returned.
///
/// inspect_repo: the DiagnosticsDataRepository which holds the access-points for all relevant
///               inspect data.
#[derive(Clone)]
pub struct ReaderServer {
    pub inspect_repo: Arc<RwLock<DiagnosticsDataRepository>>,
    pub configured_selectors: Option<Vec<Arc<fidl_fuchsia_diagnostics::Selector>>>,
    pub inspect_reader_server_stats: Arc<diagnostics::DiagnosticsServerStats>,
    /// The format requested by the client.
    format: Format,
    /// Configuration specifying max number of seconds to wait for a single
    /// component's diagnostic data as defined in StreamParameters.
    batch_retrieval_timeout_seconds: Option<i64>,
}

fn convert_snapshot_to_node_hierarchy(snapshot: ReadSnapshot) -> Result<NodeHierarchy, Error> {
    match snapshot {
        ReadSnapshot::Single(snapshot) => Ok(PartialNodeHierarchy::try_from(snapshot)?.into()),
        ReadSnapshot::Tree(snapshot_tree) => snapshot_tree.try_into(),
        ReadSnapshot::Finished(hierarchy) => Ok(hierarchy),
    }
}

pub struct BatchResultItem {
    /// Relative moniker of the component associated with this result.
    pub moniker: Moniker,
    /// The url with which the component associated with this result was launched.
    pub component_url: String,
    /// The resulting Node hierarchy plus some metadata.
    pub hierarchy_data: NodeHierarchyData,
}

impl ReaderServer {
    pub fn new(
        inspect_repo: Arc<RwLock<DiagnosticsDataRepository>>,
        format: Format,
        batch_retrieval_timeout_seconds: Option<i64>,
        configured_selectors: Option<Vec<fidl_fuchsia_diagnostics::Selector>>,
        inspect_reader_server_stats: Arc<DiagnosticsServerStats>,
    ) -> Self {
        ReaderServer {
            inspect_repo,
            format,
            batch_retrieval_timeout_seconds,
            configured_selectors: configured_selectors.map(|selectors| {
                selectors.into_iter().map(|selector| Arc::new(selector)).collect()
            }),
            inspect_reader_server_stats,
        }
    }

    fn filter_single_components_snapshots(
        snapshots: Vec<SnapshotData>,
        static_matcher: Option<InspectHierarchyMatcher>,
        client_matcher: Option<InspectHierarchyMatcher>,
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
                                        errors: vec![schema::Error { message: format!("{:?}", e) }],
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

        match client_matcher {
            // If matcher is present, and there was an InspectHierarchyMatcher,
            // then this means the client provided their own selectors, and a subset of
            // them matched this component. So we need to filter each of the snapshots from
            // this component with the dynamically provided components.
            Some(dynamic_matcher) => statically_filtered_hierarchies
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
                            Err(e) => NodeHierarchyData {
                                filename: node_hierarchy_data.filename,
                                timestamp: node_hierarchy_data.timestamp,
                                errors: vec![schema::Error { message: format!("{:?}", e) }],
                                hierarchy: None,
                            },
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
            None => statically_filtered_hierarchies,
        }
    }

    /// Takes a batch of unpopulated inspect data containers and returns a receiver that emits the
    /// populated versions of those containers.
    ///
    /// Each `PopulatedInspectDataContainer` in the output is the result of traversing each
    /// `UnpopulatedInspectDataContainer` directory and snapshotting all Inspect hierarchies.
    ///
    /// The receiver channel is eagerly populated with a limited number of populated containers (up
    /// to `MAXIMUM_SIMULTANEOUS_SNAPSHOTS_PER_READER`). The receiver stream must be continually
    /// polled to continue the snapshotting process.
    ///
    /// An entry is only an Error if connecting to the directory fails. Within a component's
    /// diagnostics directory, individual snapshots of hierarchies can fail and the transformation
    /// to a PopulatedInspectDataContainer will still succeed.
    fn pump_inspect_data(
        &self,
        inspect_batch: Vec<UnpopulatedInspectDataContainer>,
    ) -> Receiver<PopulatedInspectDataContainer> {
        let (mut sender, receiver) = channel(constants::MAXIMUM_SIMULTANEOUS_SNAPSHOTS_PER_READER);
        let global_stats = self.inspect_reader_server_stats.global_stats().clone();
        let batch_retrieval_timeout_seconds = self.batch_retrieval_timeout_seconds();
        fasync::Task::spawn(async move {
            for fut in inspect_batch.into_iter().map(move |inspect_data_packet| {
                let attempted_relative_moniker = inspect_data_packet.relative_moniker.clone();
                let attempted_inspect_matcher = inspect_data_packet.inspect_matcher.clone();
                let component_url = inspect_data_packet.component_url.clone();
                let global_stats = global_stats.clone();
                PopulatedInspectDataContainer::from(inspect_data_packet).on_timeout(
                    batch_retrieval_timeout_seconds.seconds().after_now(),
                    move || {
                        global_stats.add_timeout();
                        let error_string = format!(
                            "Exceeded per-component time limit for fetching diagnostics data: {:?}",
                            attempted_relative_moniker
                        );
                        error!("{}", error_string);
                        let no_success_snapshot_data = vec![SnapshotData::failed(
                            schema::Error { message: error_string },
                            "NO_FILE_SUCCEEDED".to_string(),
                        )];
                        PopulatedInspectDataContainer {
                            relative_moniker: attempted_relative_moniker,
                            component_url,
                            snapshots: no_success_snapshot_data,
                            inspect_matcher: attempted_inspect_matcher,
                        }
                    },
                )
            }) {
                if sender.send(fut.await).await.is_err() {
                    // The other side probably disconnected. This is not an error.
                    break;
                }
            }
        })
        .detach();
        receiver
    }

    /// Takes a PopulatedInspectDataContainer and converts all non-error
    /// results into in-memory node hierarchies. The hierarchies are filtered
    /// such that the only diagnostics properties they contain are those
    /// configured by the static and client-provided selectors.
    ///
    // TODO(fxbug.dev/4601): Error entries should still be included, but with a custom hierarchy
    //             that makes it clear to clients that snapshotting failed.
    pub fn filter_snapshot(
        &self,
        pumped_inspect_data: PopulatedInspectDataContainer,
    ) -> Vec<BatchResultItem> {
        // Since a single PopulatedInspectDataContainer shares a moniker for all pieces of data it
        // contains, we can store the result of component selector filtering to avoid reapplying
        // the selectors.
        let mut client_selectors: Option<InspectHierarchyMatcher> = None;

        // We iterate the vector of pumped inspect data packets, consuming each inspect vmo
        // and filtering it using the provided selector regular expressions. Each filtered
        // inspect hierarchy is then added to an accumulator as a HierarchyData to be converted
        // into a JSON string and returned.
        let sanitized_moniker = pumped_inspect_data
            .relative_moniker
            .iter()
            .map(|s| selectors::sanitize_string_for_selectors(s))
            .collect::<Vec<String>>()
            .join("/");

        if let Some(configured_selectors) = &self.configured_selectors {
            client_selectors = {
                let matching_selectors = selectors::match_component_moniker_against_selectors(
                    &pumped_inspect_data.relative_moniker,
                    configured_selectors,
                )
                .unwrap_or_else(|err| {
                    error!(
                        "Failed to evaluate client selectors for: {:?} Error: {:?}",
                        pumped_inspect_data.relative_moniker, err
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
            };

            // If there were configured matchers and none of them matched
            // this component, then we should return early since there is no data to
            // extract.
            if client_selectors.is_none() {
                return vec![];
            }
        }

        let component_url = pumped_inspect_data.component_url;
        ReaderServer::filter_single_components_snapshots(
            pumped_inspect_data.snapshots,
            pumped_inspect_data.inspect_matcher,
            client_selectors,
        )
        .into_iter()
        .map(|hierarchy_data| BatchResultItem {
            moniker: sanitized_moniker.clone(),
            component_url: component_url.clone(),
            hierarchy_data,
        })
        .collect()
    }

    /// Takes a vector of HierarchyData structs, and a `fidl_fuchsia_diagnostics/Format`
    /// enum, and writes each diagnostics hierarchy into a READ_ONLY VMO according to
    /// provided format. This VMO is then packaged into a `fidl_fuchsia_mem/Buffer`
    /// which is then packaged into a `fidl_fuchsia_diagnostics/FormattedContent`
    /// xunion which specifies the format of the VMO for clients.
    ///
    /// Errors in the returned Vector correspond to IO failures in writing to a VMO. If
    /// a node hierarchy fails to format, its vmo is an empty string.
    fn format_hierarchy(
        format: &Format,
        batch_item: BatchResultItem,
    ) -> Result<fidl_fuchsia_diagnostics::FormattedContent, Error> {
        let inspect_data = Data::for_inspect(
            batch_item.moniker,
            batch_item.hierarchy_data.hierarchy,
            batch_item.hierarchy_data.timestamp.into_nanos(),
            batch_item.component_url,
            batch_item.hierarchy_data.filename,
            batch_item.hierarchy_data.errors,
        );

        anyhow::ensure!(matches!(format, Format::Json), "only JSON is supported right now");
        formatter::serialize_to_formatted_json_content(inspect_data)
    }

    fn batch_retrieval_timeout_seconds(&self) -> i64 {
        match self.batch_retrieval_timeout_seconds {
            Some(batch_retrieval_timeout_seconds) => batch_retrieval_timeout_seconds,
            None => constants::PER_COMPONENT_ASYNC_TIMEOUT_SECONDS,
        }
    }
}

#[async_trait]
impl DiagnosticsServer for ReaderServer {
    fn stats(&self) -> &Arc<DiagnosticsServerStats> {
        &self.inspect_reader_server_stats
    }

    fn format(&self) -> &Format {
        &self.format
    }

    fn mode(&self) -> &StreamMode {
        &StreamMode::Snapshot
    }

    /// Takes a BatchIterator server channel and starts serving snapshotted
    /// lifecycle events as vectors of FormattedContent. The hierarchies
    /// are served in batches of `IN_MEMORY_SNAPSHOT_LIMIT` at a time, and snapshots of
    /// diagnostics data aren't taken until a component is included in the upcoming batch.
    ///
    /// NOTE: This API does not send the terminal empty-vector at the end of the snapshot.
    async fn snapshot(&self, stream: &mut BatchIteratorRequestStream) -> Result<(), Error> {
        if stream.is_terminated() {
            return Ok(());
        }

        // We must fetch the repositories in a closure to prevent the
        // repository mutex-guard from leaking into futures.
        let inspect_repo_data =
            self.inspect_repo.read().fetch_inspect_data(&self.configured_selectors);

        let mut data_stream = self
            .pump_inspect_data(inspect_repo_data)
            .fuse() // Ensure that the read following the end of the stream does not panic
            .map(|populated_data_container| {
                stream::iter(self.filter_snapshot(populated_data_container).into_iter())
            })
            .flatten()
            .filter_map(|batch_item| async {
                if !batch_item.hierarchy_data.errors.is_empty() {
                    self.stats().add_result_error();
                }
                self.stats().add_result();

                ReaderServer::format_hierarchy(self.format(), batch_item).ok()
            })
            .boxed();

        while let Some(req) = stream.try_next().await? {
            match req {
                fidl_fuchsia_diagnostics::BatchIteratorRequest::GetNext { responder } => {
                    self.stats().add_request();
                    let filtered_results = data_stream
                        .by_ref()
                        .take(constants::IN_MEMORY_SNAPSHOT_LIMIT)
                        .collect::<Vec<_>>()
                        .await;

                    if filtered_results.is_empty() {
                        // Nothing remains in the repository.
                        self.stats().add_response();
                        self.stats().add_terminal();
                        responder.send(&mut Ok(Vec::new()))?;
                        return Ok(());
                    }

                    self.stats().add_response();
                    responder.send(&mut Ok(filtered_results))?;
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::collector::InspectDataCollector,
        super::*,
        crate::{
            events::types::{ComponentIdentifier, InspectData, LegacyIdentifier, RealmPath},
            logs::LogManager,
        },
        anyhow::format_err,
        fdio,
        fidl::endpoints::create_proxy,
        fidl::endpoints::DiscoverableService,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_inspect::TreeMarker,
        fidl_fuchsia_io::DirectoryMarker,
        fuchsia_async::{self as fasync, DurationExt},
        fuchsia_component::server::ServiceFs,
        fuchsia_inspect::{assert_inspect_tree, reader, Inspector},
        fuchsia_inspect_node_hierarchy::{trie::TrieIterableNode, NodeHierarchy},
        fuchsia_zircon as zx,
        fuchsia_zircon::Peered,
        futures::future::join_all,
        futures::{FutureExt, StreamExt},
        serde_json::json,
        std::path::PathBuf,
    };

    const TEST_URL: &'static str = "fuchsia-pkg://test";
    const BATCH_RETRIEVAL_TIMEOUT_SECONDS: Option<i64> = Some(300);

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

        fasync::Task::spawn(fs.collect()).detach();

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

        fasync::Task::spawn(fs.collect()).detach();

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

        fasync::Task::spawn(fs.collect()).detach();
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

        fasync::Task::spawn(fs.collect()).detach();
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
    async fn reader_server_reports_errors() {
        let path = PathBuf::from("/test-bindings-errors-01");

        // Make a ServiceFs containing something that looks like an inspect file but is not.
        let mut fs = ServiceFs::new();
        let vmo = zx::Vmo::create(4096).unwrap();
        fs.dir("diagnostics").add_vmo_file_at("test.inspect", vmo, 0, 4096);

        // Create a connection to the ServiceFs.
        let (h0, h1) = zx::Channel::create().unwrap();
        fs.serve_connection(h1).unwrap();

        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.join("out").to_str().unwrap(), h0).unwrap();

        fasync::Task::spawn(fs.collect()).detach();
        let (done0, done1) = zx::Channel::create().unwrap();
        let thread_path = path.join("out");

        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let path = thread_path;
            let done = done1;
            let mut executor = fasync::Executor::new().unwrap();
            executor.run_singlethreaded(async {
                verify_reader_with_mode(path, VerifyMode::ExpectComponentFailure).await;
                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn inspect_repo_disallows_duplicated_dirs() {
        let mut inspect_repo = DiagnosticsDataRepository::new(LogManager::new(), None);
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });
        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        inspect_repo
            .add_inspect_artifacts(component_id.clone(), TEST_URL, proxy, zx::Time::from_nanos(0))
            .expect("add to repo");

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        inspect_repo
            .add_inspect_artifacts(component_id.clone(), TEST_URL, proxy, zx::Time::from_nanos(0))
            .expect("add to repo");

        let key = component_id.unique_key();
        assert_eq!(inspect_repo.data_directories.get(key).unwrap().get_values().len(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn canonical_inspect_reader_stress_test() {
        // Test that 3 directories, each with 33 vmos, has snapshots served over 3 batches
        // each of which contains the 33 vmos of one component.
        stress_test_diagnostics_repository(vec![33, 33, 33], vec![64, 35]).await;

        // The 64 entry vmo is served by itself, and the 63 vmo and 1 vmo directories are combined.
        stress_test_diagnostics_repository(vec![64, 63, 1], vec![64, 64]).await;

        // 64 1vmo components are sent in one batch.
        stress_test_diagnostics_repository([1usize; 64].to_vec(), vec![64]).await;

        // A component with > the maximum batch size is split in two batches.
        stress_test_diagnostics_repository(vec![65], vec![64, 1]).await;

        // An errorful component doesn't halt iteration.
        stress_test_diagnostics_repository(vec![64, 65, 64, 64], vec![64, 64, 64, 64, 1]).await;

        // An errorful component can be merged into a batch where it may fit.
        stress_test_diagnostics_repository(vec![63, 65], vec![64, 64]).await;
    }

    async fn stress_test_diagnostics_repository(
        directory_vmo_counts: Vec<usize>,
        expected_batch_results: Vec<usize>,
    ) {
        let path = PathBuf::from("/stress_test_root_directory");

        let dir_name_and_filecount: Vec<(String, usize)> = directory_vmo_counts
            .into_iter()
            .enumerate()
            .map(|(index, filecount)| (format!("diagnostics_{}", index), filecount))
            .collect();

        // Make a ServiceFs that will host inspect vmos under each
        // of the new diagnostics directories.
        let mut fs = ServiceFs::new();
        let log_manager = LogManager::new();

        let inspector = inspector_for_reader_test();

        for (directory_name, filecount) in dir_name_and_filecount.clone() {
            for i in 0..filecount {
                let vmo = inspector
                    .duplicate_vmo()
                    .ok_or(format_err!("Failed to duplicate VMO"))
                    .unwrap();

                let size = vmo.get_size().unwrap();
                fs.dir(directory_name.clone()).add_vmo_file_at(
                    format!("root_{}.inspect", i),
                    vmo,
                    0, /* vmo offset */
                    size,
                );
            }
        }
        let (h0, h1) = zx::Channel::create().unwrap();
        fs.serve_connection(h1).unwrap();

        // We bind the root of the FS that hosts our 3 test dirs to
        // stress_test_root_dir. Now each directory can be found at
        // stress_test_root_dir/diagnostics_<i>
        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.to_str().unwrap(), h0).unwrap();

        fasync::Task::spawn(fs.collect()).detach();

        let (done0, done1) = zx::Channel::create().unwrap();

        let cloned_path = path.clone();
        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let done = done1;
            let mut executor = fasync::Executor::new().unwrap();

            executor.run_singlethreaded(async {
                let id_and_directory_proxy =
                    join_all(dir_name_and_filecount.iter().map(|(dir, _)| {
                        let new_async_clone = cloned_path.clone();
                        async move {
                            let full_path = new_async_clone.join(dir);
                            let proxy = InspectDataCollector::find_directory_proxy(&full_path)
                                .await
                                .unwrap();
                            let unique_cid = ComponentIdentifier::Legacy(LegacyIdentifier {
                                instance_id: "1234".into(),
                                realm_path: vec![].into(),
                                component_name: format!("component_{}.cmx", dir).into(),
                            });
                            (unique_cid, proxy)
                        }
                    }))
                    .await;

                let inspect_repo = Arc::new(RwLock::new(DiagnosticsDataRepository::new(
                    log_manager.clone(),
                    None,
                )));

                for (cid, proxy) in id_and_directory_proxy {
                    inspect_repo
                        .write()
                        .add_inspect_artifacts(
                            cid.clone(),
                            TEST_URL,
                            proxy,
                            zx::Time::from_nanos(0),
                        )
                        .unwrap();
                }

                let inspector = Inspector::new();
                let root = inspector.root();
                let test_archive_accessor_node = root.create_child("test_archive_accessor_node");

                let test_accessor_stats =
                    Arc::new(diagnostics::ArchiveAccessorStats::new(test_archive_accessor_node));
                let test_batch_iterator_stats1 = Arc::new(
                    diagnostics::DiagnosticsServerStats::for_inspect(test_accessor_stats.clone()),
                );

                let reader_server = ReaderServer::new(
                    inspect_repo.clone(),
                    Format::Json,
                    BATCH_RETRIEVAL_TIMEOUT_SECONDS,
                    None,
                    test_batch_iterator_stats1.clone(),
                );
                let _result_json = read_snapshot_verify_batch_count_and_batch_size(
                    reader_server,
                    expected_batch_results,
                )
                .await;

                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.to_str().unwrap()).unwrap();
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

    enum VerifyMode {
        ExpectSuccess,
        ExpectComponentFailure,
    }

    async fn verify_reader(path: PathBuf) {
        verify_reader_with_mode(path, VerifyMode::ExpectSuccess).await;
    }

    async fn verify_reader_with_mode(path: PathBuf, mode: VerifyMode) {
        let child_1_1_selector = selectors::parse_selector(r#"*:root/child_1/*:some-int"#).unwrap();
        let child_2_selector =
            selectors::parse_selector(r#"test_component.cmx:root/child_2:*"#).unwrap();
        let inspect_repo = Arc::new(RwLock::new(DiagnosticsDataRepository::new(
            LogManager::new(),
            Some(vec![Arc::new(child_1_1_selector), Arc::new(child_2_selector)]),
        )));

        let out_dir_proxy = InspectDataCollector::find_directory_proxy(&path).await.unwrap();

        // The absolute moniker here is made up since the selector is a glob
        // selector, so any path would match.
        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id: "1234".into(),
            realm_path: vec![].into(),
            component_name: "test_component.cmx".into(),
        });

        let inspector = Inspector::new();
        let root = inspector.root();
        let test_archive_accessor_node = root.create_child("test_archive_accessor_node");

        assert_inspect_tree!(inspector, root: {test_archive_accessor_node: {}});

        let test_accessor_stats =
            Arc::new(diagnostics::ArchiveAccessorStats::new(test_archive_accessor_node));

        let test_batch_iterator_stats1 =
            Arc::new(diagnostics::DiagnosticsServerStats::for_inspect(test_accessor_stats.clone()));

        assert_inspect_tree!(inspector, root: {
            test_archive_accessor_node: {
                archive_accessor_connections_closed: 0u64,
                archive_accessor_connections_opened: 0u64,
                inspect_batch_iterator_connection0:{
                    inspect_batch_iterator_terminal_responses: 0u64,
                    inspect_batch_iterator_get_next_responses: 0u64,
                    inspect_batch_iterator_get_next_requests: 0u64,
                },
                inspect_batch_iterator_connections_closed: 0u64,
                inspect_batch_iterator_connections_opened: 0u64,
                inspect_batch_iterator_get_next_errors: 0u64,
                inspect_batch_iterator_get_next_requests: 0u64,
                inspect_batch_iterator_get_next_responses: 0u64,
                inspect_batch_iterator_get_next_result_count: 0u64,
                inspect_batch_iterator_get_next_result_errors: 0u64,
                inspect_component_timeouts_count: 0u64,
                inspect_reader_servers_constructed: 1u64,
                inspect_reader_servers_destroyed: 0u64,
                lifecycle_batch_iterator_connections_closed: 0u64,
                lifecycle_batch_iterator_connections_opened: 0u64,
                lifecycle_batch_iterator_get_next_errors: 0u64,
                lifecycle_batch_iterator_get_next_requests: 0u64,
                lifecycle_batch_iterator_get_next_responses: 0u64,
                lifecycle_batch_iterator_get_next_result_count: 0u64,
                lifecycle_batch_iterator_get_next_result_errors: 0u64,
                lifecycle_component_timeouts_count: 0u64,
                lifecycle_reader_servers_constructed: 0u64,
                lifecycle_reader_servers_destroyed: 0u64,
                logs_batch_iterator_connections_closed: 0u64,
                logs_batch_iterator_connections_opened: 0u64,
                logs_batch_iterator_get_next_errors: 0u64,
                logs_batch_iterator_get_next_requests: 0u64,
                logs_batch_iterator_get_next_responses: 0u64,
                logs_batch_iterator_get_next_result_count: 0u64,
                logs_batch_iterator_get_next_result_errors: 0u64,
                logs_component_timeouts_count: 0u64,
                logs_reader_servers_constructed: 0u64,
                logs_reader_servers_destroyed: 0u64,
                stream_diagnostics_requests: 0u64,
            }
        });

        let inspector_arc = Arc::new(inspector);

        inspect_repo
            .write()
            .add_inspect_artifacts(
                component_id.clone(),
                TEST_URL,
                out_dir_proxy,
                zx::Time::from_nanos(0),
            )
            .unwrap();

        let expected_get_next_result_errors = match mode {
            VerifyMode::ExpectComponentFailure => 1u64,
            _ => 0u64,
        };

        {
            let reader_server = ReaderServer::new(
                inspect_repo.clone(),
                Format::Json,
                BATCH_RETRIEVAL_TIMEOUT_SECONDS,
                None,
                test_batch_iterator_stats1,
            );

            let result_json = read_snapshot(reader_server, inspector_arc.clone()).await;

            let result_array = result_json.as_array().expect("unit test json should be array.");
            assert_eq!(result_array.len(), 1, "Expect only one schema to be returned.");

            let result_map =
                result_array[0].as_object().expect("entries in the schema array are json objects.");

            let result_payload =
                result_map.get("payload").expect("diagnostics schema requires payload entry.");

            let expected_payload = match mode {
                VerifyMode::ExpectSuccess => json!({
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
                }),
                VerifyMode::ExpectComponentFailure => json!(null),
            };
            assert_eq!(*result_payload, expected_payload);

            // stream_diagnostics_requests is 0 since its tracked via archive_accessor server,
            // which isnt running in this unit test.
            assert_inspect_tree!(inspector_arc.clone(), root: {
                test_archive_accessor_node: {
                    archive_accessor_connections_closed: 0u64,
                    archive_accessor_connections_opened: 0u64,
                    inspect_batch_iterator_connection0:{
                        inspect_batch_iterator_terminal_responses: 1u64,
                        inspect_batch_iterator_get_next_responses: 2u64,
                        inspect_batch_iterator_get_next_requests: 2u64,
                    },
                    inspect_batch_iterator_connections_closed: 0u64,
                    inspect_batch_iterator_connections_opened: 1u64,
                    inspect_batch_iterator_get_next_errors: 0u64,
                    inspect_batch_iterator_get_next_requests: 2u64,
                    inspect_batch_iterator_get_next_responses: 2u64,
                    inspect_batch_iterator_get_next_result_count: 1u64,
                    inspect_batch_iterator_get_next_result_errors: expected_get_next_result_errors,
                    inspect_component_timeouts_count: 0u64,
                    inspect_reader_servers_constructed: 1u64,
                    inspect_reader_servers_destroyed: 0u64,
                    lifecycle_batch_iterator_connections_closed: 0u64,
                    lifecycle_batch_iterator_connections_opened: 0u64,
                    lifecycle_batch_iterator_get_next_errors: 0u64,
                    lifecycle_batch_iterator_get_next_requests: 0u64,
                    lifecycle_batch_iterator_get_next_responses: 0u64,
                    lifecycle_batch_iterator_get_next_result_count: 0u64,
                    lifecycle_batch_iterator_get_next_result_errors: 0u64,
                    lifecycle_component_timeouts_count: 0u64,
                    lifecycle_reader_servers_constructed: 0u64,
                    lifecycle_reader_servers_destroyed: 0u64,
                    logs_batch_iterator_connections_closed: 0u64,
                    logs_batch_iterator_connections_opened: 0u64,
                    logs_batch_iterator_get_next_errors: 0u64,
                    logs_batch_iterator_get_next_requests: 0u64,
                    logs_batch_iterator_get_next_responses: 0u64,
                    logs_batch_iterator_get_next_result_count: 0u64,
                    logs_batch_iterator_get_next_result_errors: 0u64,
                    logs_component_timeouts_count: 0u64,
                    logs_reader_servers_constructed: 0u64,
                    logs_reader_servers_destroyed: 0u64,
                    stream_diagnostics_requests: 0u64,
                }
            });
        }

        // There is a race between the RAII destruction of the reader server which must make
        // one more try_next call after the client channel is destroyed at the end of read_snapshot
        // and the inspector seeing both that reader server desruction and the termination of the
        // batch iterator connection.
        wait_for_reader_service_cleanup(inspector_arc.clone(), 1).await;

        // we should see that the reader server has been destroyed.
        assert_inspect_tree!(inspector_arc.clone(), root: {
            test_archive_accessor_node: {
                archive_accessor_connections_closed: 0u64,
                archive_accessor_connections_opened: 0u64,
                inspect_batch_iterator_connections_closed: 1u64,
                inspect_batch_iterator_connections_opened: 1u64,
                inspect_batch_iterator_get_next_errors: 0u64,
                inspect_batch_iterator_get_next_requests: 2u64,
                inspect_batch_iterator_get_next_responses: 2u64,
                inspect_batch_iterator_get_next_result_count: 1u64,
                inspect_batch_iterator_get_next_result_errors: expected_get_next_result_errors,
                inspect_component_timeouts_count: 0u64,
                inspect_reader_servers_constructed: 1u64,
                inspect_reader_servers_destroyed: 1u64,
                lifecycle_batch_iterator_connections_closed: 0u64,
                lifecycle_batch_iterator_connections_opened: 0u64,
                lifecycle_batch_iterator_get_next_errors: 0u64,
                lifecycle_batch_iterator_get_next_requests: 0u64,
                lifecycle_batch_iterator_get_next_responses: 0u64,
                lifecycle_batch_iterator_get_next_result_count: 0u64,
                lifecycle_batch_iterator_get_next_result_errors: 0u64,
                lifecycle_component_timeouts_count: 0u64,
                lifecycle_reader_servers_constructed: 0u64,
                lifecycle_reader_servers_destroyed: 0u64,
                logs_batch_iterator_connections_closed: 0u64,
                logs_batch_iterator_connections_opened: 0u64,
                logs_batch_iterator_get_next_errors: 0u64,
                logs_batch_iterator_get_next_requests: 0u64,
                logs_batch_iterator_get_next_responses: 0u64,
                logs_batch_iterator_get_next_result_count: 0u64,
                logs_batch_iterator_get_next_result_errors: 0u64,
                logs_component_timeouts_count: 0u64,
                logs_reader_servers_constructed: 0u64,
                logs_reader_servers_destroyed: 0u64,
                stream_diagnostics_requests: 0u64,
            }
        });

        let test_batch_iterator_stats2 =
            Arc::new(diagnostics::DiagnosticsServerStats::for_inspect(test_accessor_stats.clone()));

        inspect_repo.write().remove(&component_id);
        {
            let reader_server = ReaderServer::new(
                inspect_repo.clone(),
                Format::Json,
                BATCH_RETRIEVAL_TIMEOUT_SECONDS,
                None,
                test_batch_iterator_stats2,
            );
            let result_json = read_snapshot(reader_server, inspector_arc.clone()).await;

            let result_array = result_json.as_array().expect("unit test json should be array.");
            assert_eq!(result_array.len(), 0, "Expect no schemas to be returned.");

            assert_inspect_tree!(inspector_arc.clone(), root: {
                test_archive_accessor_node: {
                    archive_accessor_connections_closed: 0u64,
                    archive_accessor_connections_opened: 0u64,
                    inspect_batch_iterator_connection1:{
                        inspect_batch_iterator_terminal_responses: 1u64,
                        inspect_batch_iterator_get_next_responses: 1u64,
                        inspect_batch_iterator_get_next_requests: 1u64,
                    },
                    inspect_batch_iterator_connections_closed: 1u64,
                    inspect_batch_iterator_connections_opened: 2u64,
                    inspect_batch_iterator_get_next_errors: 0u64,
                    inspect_batch_iterator_get_next_requests: 3u64,
                    inspect_batch_iterator_get_next_responses: 3u64,
                    inspect_batch_iterator_get_next_result_count: 1u64,
                    inspect_batch_iterator_get_next_result_errors: expected_get_next_result_errors,
                    inspect_component_timeouts_count: 0u64,
                    inspect_reader_servers_constructed: 2u64,
                    inspect_reader_servers_destroyed: 1u64,
                    lifecycle_batch_iterator_connections_closed: 0u64,
                    lifecycle_batch_iterator_connections_opened: 0u64,
                    lifecycle_batch_iterator_get_next_errors: 0u64,
                    lifecycle_batch_iterator_get_next_requests: 0u64,
                    lifecycle_batch_iterator_get_next_responses: 0u64,
                    lifecycle_batch_iterator_get_next_result_count: 0u64,
                    lifecycle_batch_iterator_get_next_result_errors: 0u64,
                    lifecycle_component_timeouts_count: 0u64,
                    lifecycle_reader_servers_constructed: 0u64,
                    lifecycle_reader_servers_destroyed: 0u64,
                    logs_batch_iterator_connections_closed: 0u64,
                    logs_batch_iterator_connections_opened: 0u64,
                    logs_batch_iterator_get_next_errors: 0u64,
                    logs_batch_iterator_get_next_requests: 0u64,
                    logs_batch_iterator_get_next_responses: 0u64,
                    logs_batch_iterator_get_next_result_count: 0u64,
                    logs_batch_iterator_get_next_result_errors: 0u64,
                    logs_component_timeouts_count: 0u64,
                    logs_reader_servers_constructed: 0u64,
                    logs_reader_servers_destroyed: 0u64,
                    stream_diagnostics_requests: 0u64,
                }
            });
        }

        // There is a race between the RAII destruction of the reader server which must make
        // one more try_next call after the client channel is destroyed at the end of read_snapshot
        // and the inspector seeing both that reader server desruction and the termination of the
        // batch iterator connection.
        wait_for_reader_service_cleanup(inspector_arc.clone(), 2).await;
        assert_inspect_tree!(inspector_arc.clone(), root: {
            test_archive_accessor_node: {
                archive_accessor_connections_closed: 0u64,
                archive_accessor_connections_opened: 0u64,
                inspect_batch_iterator_connections_closed: 2u64,
                inspect_batch_iterator_connections_opened: 2u64,
                inspect_batch_iterator_get_next_errors: 0u64,
                inspect_batch_iterator_get_next_requests: 3u64,
                inspect_batch_iterator_get_next_responses: 3u64,
                inspect_batch_iterator_get_next_result_count: 1u64,
                inspect_batch_iterator_get_next_result_errors: expected_get_next_result_errors,
                inspect_component_timeouts_count: 0u64,
                inspect_reader_servers_constructed: 2u64,
                inspect_reader_servers_destroyed: 2u64,
                lifecycle_batch_iterator_connections_closed: 0u64,
                lifecycle_batch_iterator_connections_opened: 0u64,
                lifecycle_batch_iterator_get_next_errors: 0u64,
                lifecycle_batch_iterator_get_next_requests: 0u64,
                lifecycle_batch_iterator_get_next_responses: 0u64,
                lifecycle_batch_iterator_get_next_result_count: 0u64,
                lifecycle_batch_iterator_get_next_result_errors: 0u64,
                lifecycle_component_timeouts_count: 0u64,
                lifecycle_reader_servers_constructed :0u64,
                lifecycle_reader_servers_destroyed: 0u64,
                logs_batch_iterator_connections_closed: 0u64,
                logs_batch_iterator_connections_opened: 0u64,
                logs_batch_iterator_get_next_errors: 0u64,
                logs_batch_iterator_get_next_requests: 0u64,
                logs_batch_iterator_get_next_responses: 0u64,
                logs_batch_iterator_get_next_result_count: 0u64,
                logs_batch_iterator_get_next_result_errors: 0u64,
                logs_component_timeouts_count: 0u64,
                logs_reader_servers_constructed: 0u64,
                logs_reader_servers_destroyed: 0u64,
                stream_diagnostics_requests: 0u64,
            }
        });
    }

    async fn wait_for_reader_service_cleanup(
        inspector: Arc<Inspector>,
        expected_destroyed_reader_servers: u64,
    ) {
        loop {
            let inspect_hierarchy = reader::read_from_inspector(&inspector)
                .await
                .expect("test inspector should be parseable.");
            let destroyed_readers_selector = selectors::parse_selector(
                r#"*:root/test_archive_accessor_node:inspect_reader_servers_destroyed"#,
            )
            .unwrap();

            match fuchsia_inspect_node_hierarchy::select_from_node_hierarchy(
                    inspect_hierarchy,
                    destroyed_readers_selector,
                )
                .expect("Always expect selection of inspect_reader_servers_destroyed to succeed.")
                .as_slice()
                {
                    [destroyed_reader_property_entry] => {
                        match destroyed_reader_property_entry.property {
                            fuchsia_inspect_node_hierarchy::Property::Uint(_, x) => {
                                if x == expected_destroyed_reader_servers {
                                    break;
                                } else {
                                    let sleep_duration = zx::Duration::from_millis(10i64);
                                    fasync::Timer::new(sleep_duration.after_now()).await;
                                    continue;
                                }
                            },
                            _ => panic!("inspect_reader_servers_destroyed should always be a uint."),
                        }
                    },
                    _ => panic!("Test always expects exactly one inspect_reader_servers_destroyed property to be present."),
                }
        }
    }

    async fn read_snapshot(
        reader_server: ReaderServer,
        _test_inspector: Arc<Inspector>,
    ) -> serde_json::Value {
        let (consumer, batch_iterator): (
            _,
            ServerEnd<fidl_fuchsia_diagnostics::BatchIteratorMarker>,
        ) = create_proxy().unwrap();

        reader_server.spawn(batch_iterator).detach();

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

    async fn read_snapshot_verify_batch_count_and_batch_size(
        reader_server: ReaderServer,
        expected_batch_sizes: Vec<usize>,
    ) -> serde_json::Value {
        let (consumer, batch_iterator): (
            _,
            ServerEnd<fidl_fuchsia_diagnostics::BatchIteratorMarker>,
        ) = create_proxy().unwrap();

        reader_server.spawn(batch_iterator).detach();

        let mut result_vec: Vec<String> = Vec::new();
        let mut batch_counts = Vec::new();
        loop {
            let next_batch: Vec<fidl_fuchsia_diagnostics::FormattedContent> =
                consumer.get_next().await.unwrap().unwrap();

            if next_batch.is_empty() {
                assert_eq!(expected_batch_sizes, batch_counts);
                break;
            }

            batch_counts.push(next_batch.len());

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
