// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        accessor::PerformanceConfig,
        constants,
        diagnostics::BatchIteratorConnectionStats,
        inspect::container::{ReadSnapshot, SnapshotData, UnpopulatedInspectDataContainer},
        moniker_rewriter::OutputRewriter,
        ImmutableString,
    },
    diagnostics_data::{self as schema, Data, Inspect},
    diagnostics_hierarchy::{DiagnosticsHierarchy, InspectHierarchyMatcher},
    fidl_fuchsia_diagnostics::{self, Selector},
    fuchsia_inspect::reader::PartialNodeHierarchy,
    fuchsia_trace as ftrace, fuchsia_zircon as zx,
    futures::prelude::*,
    std::{
        convert::{TryFrom, TryInto},
        sync::Arc,
    },
    tracing::error,
};

pub mod collector;
pub mod container;
pub mod repository;

use container::PopulatedInspectDataContainer;

/// Packet containing a node hierarchy and all the metadata needed to
/// populate a diagnostics schema for that node hierarchy.
pub struct NodeHierarchyData {
    // Name of the file that created this snapshot.
    filename: ImmutableString,
    // Timestamp at which this snapshot resolved or failed.
    timestamp: zx::Time,
    // Errors encountered when processing this snapshot.
    errors: Vec<schema::InspectError>,
    // Optional DiagnosticsHierarchy of the inspect hierarchy, in case reading fails
    // and we have errors to share with client.
    hierarchy: Option<DiagnosticsHierarchy>,
}

impl From<SnapshotData> for NodeHierarchyData {
    fn from(data: SnapshotData) -> NodeHierarchyData {
        match data.snapshot {
            Some(snapshot) => match convert_snapshot_to_node_hierarchy(snapshot) {
                Ok(node_hierarchy) => NodeHierarchyData {
                    filename: data.filename,
                    timestamp: data.timestamp,
                    errors: data.errors,
                    hierarchy: Some(node_hierarchy),
                },
                Err(e) => NodeHierarchyData {
                    filename: data.filename,
                    timestamp: data.timestamp,
                    errors: vec![schema::InspectError { message: format!("{:?}", e) }],
                    hierarchy: None,
                },
            },
            None => NodeHierarchyData {
                filename: data.filename,
                timestamp: data.timestamp,
                errors: data.errors,
                hierarchy: None,
            },
        }
    }
}

/// ReaderServer holds the state and data needed to serve Inspect data
/// reading requests for a single client.
pub struct ReaderServer {
    /// Selectors provided by the client which define what inspect data is returned by read
    /// requests. A none type implies that all available data should be returned.
    selectors: Option<Vec<Selector>>,
    output_rewriter: Option<OutputRewriter>,
}

fn convert_snapshot_to_node_hierarchy(
    snapshot: ReadSnapshot,
) -> Result<DiagnosticsHierarchy, fuchsia_inspect::reader::ReaderError> {
    match snapshot {
        ReadSnapshot::Single(snapshot) => Ok(PartialNodeHierarchy::try_from(snapshot)?.into()),
        ReadSnapshot::Tree(snapshot_tree) => snapshot_tree.try_into(),
        ReadSnapshot::Finished(hierarchy) => Ok(hierarchy),
    }
}

impl ReaderServer {
    /// Create a stream of filtered inspect data, ready to serve.
    pub fn stream(
        unpopulated_diagnostics_sources: Vec<UnpopulatedInspectDataContainer>,
        performance_configuration: PerformanceConfig,
        selectors: Option<Vec<Selector>>,
        output_rewriter: Option<OutputRewriter>,
        stats: Arc<BatchIteratorConnectionStats>,
        parent_trace_id: ftrace::Id,
    ) -> impl Stream<Item = Data<Inspect>> + Send + 'static {
        let server = Arc::new(Self { selectors, output_rewriter });

        let batch_timeout = performance_configuration.batch_timeout_sec;

        futures::stream::iter(unpopulated_diagnostics_sources.into_iter())
            .map(move |unpopulated| {
                let global_stats = stats.global_stats().clone();
                unpopulated.populate(batch_timeout, global_stats, parent_trace_id)
            })
            .flatten()
            .map(future::ready)
            // buffer a small number in memory in case later components time out
            .buffer_unordered(constants::MAXIMUM_SIMULTANEOUS_SNAPSHOTS_PER_READER)
            // filter each component's inspect
            .filter_map(move |populated| {
                let server_clone = server.clone();
                async move { server_clone.filter_snapshot(populated, parent_trace_id) }
            })
    }

    fn filter_single_components_snapshot(
        snapshot_data: SnapshotData,
        static_matcher: Option<InspectHierarchyMatcher>,
        client_matcher: Option<InspectHierarchyMatcher>,
        moniker: &str,
        parent_trace_id: ftrace::Id,
    ) -> NodeHierarchyData {
        let filename = snapshot_data.filename.clone();
        let node_hierarchy_data = match static_matcher {
            // The only way we have a None value for the PopulatedDataContainer is
            // if there were no provided static selectors, which is only valid in
            // the AllAccess pipeline. For all other pipelines, if no static selectors
            // matched, the data wouldn't have ended up in the repository to begin
            // with.
            None => {
                let trace_id = ftrace::Id::random();
                let _trace_guard = ftrace::async_enter!(
                    trace_id,
                    "app",
                    "SnapshotData -> NodeHierarchyData",
                    // An async duration cannot have multiple concurrent child async durations
                    // so we include the nonce as metadata to manually determine relationship.
                    "parent_trace_id" => u64::from(parent_trace_id),
                    "trace_id" => u64::from(trace_id),
                    "moniker" => moniker,
                    "filename" => filename.as_ref()
                );
                snapshot_data.into()
            }
            Some(static_matcher) => {
                let node_hierarchy_data: NodeHierarchyData = {
                    let trace_id = ftrace::Id::random();
                    let _trace_guard = ftrace::async_enter!(
                        trace_id,
                        "app",
                        "SnapshotData -> NodeHierarchyData",
                        // An async duration cannot have multiple concurrent child async durations
                        // so we include the nonce as metadata to manually determine relationship.
                        "parent_trace_id" => u64::from(parent_trace_id),
                        "trace_id" => u64::from(trace_id),
                        "moniker" => moniker,
                        "filename" => filename.as_ref()
                    );
                    snapshot_data.into()
                };

                match node_hierarchy_data.hierarchy {
                    Some(node_hierarchy) => {
                        let trace_id = ftrace::Id::random();
                        let _trace_guard = ftrace::async_enter!(
                            trace_id,
                            "app",
                            "ReaderServer::filter_single_components_snapshot.filter_hierarchy",
                            // An async duration cannot have multiple concurrent child async durations
                            // so we include the nonce as metadata to manually determine relationship.
                            "parent_trace_id" => u64::from(parent_trace_id),
                            "trace_id" => u64::from(trace_id),
                            "moniker" => moniker,
                            "filename" => node_hierarchy_data.filename.as_ref(),
                            "selector_type" => "static"
                        );
                        match diagnostics_hierarchy::filter_hierarchy(
                            node_hierarchy,
                            &static_matcher,
                        ) {
                            Ok(Some(filtered_hierarchy)) => NodeHierarchyData {
                                filename: node_hierarchy_data.filename,
                                timestamp: node_hierarchy_data.timestamp,
                                errors: node_hierarchy_data.errors,
                                hierarchy: Some(filtered_hierarchy),
                            },
                            Ok(None) => NodeHierarchyData {
                                filename: node_hierarchy_data.filename,
                                timestamp: node_hierarchy_data.timestamp,
                                errors: vec![schema::InspectError {
                                    message: concat!(
                                        "Inspect hierarchy was fully filtered",
                                        " by static selectors. No data remaining."
                                    )
                                    .to_string(),
                                }],
                                hierarchy: None,
                            },
                            Err(e) => {
                                error!(?e, "Failed to filter a node hierarchy");
                                NodeHierarchyData {
                                    filename: node_hierarchy_data.filename,
                                    timestamp: node_hierarchy_data.timestamp,
                                    errors: vec![schema::InspectError {
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
            }
        };

        match client_matcher {
            // If matcher is present, and there was an InspectHierarchyMatcher,
            // then this means the client provided their own selectors, and a subset of
            // them matched this component. So we need to filter each of the snapshots from
            // this component with the dynamically provided components.
            Some(dynamic_matcher) => match node_hierarchy_data.hierarchy {
                None => NodeHierarchyData {
                    filename: node_hierarchy_data.filename,
                    timestamp: node_hierarchy_data.timestamp,
                    errors: node_hierarchy_data.errors,
                    hierarchy: None,
                },
                Some(node_hierarchy) => {
                    let trace_id = ftrace::Id::random();
                    let _trace_guard = ftrace::async_enter!(
                        trace_id,
                        "app",
                        "ReaderServer::filter_single_components_snapshot.filter_hierarchy",
                        // An async duration cannot have multiple concurrent child async durations
                        // so we include the nonce as metadata to manually determine relationship.
                        "parent_trace_id" => u64::from(parent_trace_id),
                        "trace_id" => u64::from(trace_id),
                        "moniker" => moniker,
                        "filename" => node_hierarchy_data.filename.as_ref(),
                        "selector_type" => "client"
                    );
                    match diagnostics_hierarchy::filter_hierarchy(node_hierarchy, &dynamic_matcher)
                    {
                        Ok(Some(filtered_hierarchy)) => NodeHierarchyData {
                            filename: node_hierarchy_data.filename,
                            timestamp: node_hierarchy_data.timestamp,
                            errors: node_hierarchy_data.errors,
                            hierarchy: Some(filtered_hierarchy),
                        },
                        Ok(None) => NodeHierarchyData {
                            filename: node_hierarchy_data.filename,
                            timestamp: node_hierarchy_data.timestamp,
                            errors: vec![schema::InspectError {
                                message: concat!(
                                    "Inspect hierarchy was fully filtered",
                                    " by client provided selectors. No data remaining."
                                )
                                .to_string(),
                            }],
                            hierarchy: None,
                        },
                        Err(e) => NodeHierarchyData {
                            filename: node_hierarchy_data.filename,
                            timestamp: node_hierarchy_data.timestamp,
                            errors: vec![schema::InspectError { message: format!("{:?}", e) }],
                            hierarchy: None,
                        },
                    }
                }
            },
            None => node_hierarchy_data,
        }
    }

    /// Takes a PopulatedInspectDataContainer and converts all non-error
    /// results into in-memory node hierarchies. The hierarchies are filtered
    /// such that the only diagnostics properties they contain are those
    /// configured by the static and client-provided selectors.
    ///
    // TODO(fxbug.dev/4601): Error entries should still be included, but with a custom hierarchy
    //             that makes it clear to clients that snapshotting failed.
    fn filter_snapshot(
        &self,
        pumped_inspect_data: PopulatedInspectDataContainer,
        parent_trace_id: ftrace::Id,
    ) -> Option<Data<Inspect>> {
        // Since a single PopulatedInspectDataContainer shares a moniker for all pieces of data it
        // contains, we can store the result of component selector filtering to avoid reapplying
        // the selectors.
        let mut client_selectors: Option<InspectHierarchyMatcher> = None;

        // We iterate the vector of pumped inspect data packets, consuming each inspect vmo
        // and filtering it using the provided selector regular expressions. Each filtered
        // inspect hierarchy is then added to an accumulator as a HierarchyData to be converted
        // into a JSON string and returned.
        let sanitized_moniker = pumped_inspect_data
            .identity
            .relative_moniker
            .iter()
            .map(|s| selectors::sanitize_string_for_selectors(s))
            .collect::<Vec<String>>()
            .join("/");
        let sanitized_moniker = match &self.output_rewriter {
            None => sanitized_moniker,
            Some(rewriter) => rewriter.rewrite_moniker(sanitized_moniker),
        };

        if let Some(configured_selectors) = &self.selectors {
            client_selectors = {
                let matching_selectors = selectors::match_component_moniker_against_selectors(
                    &pumped_inspect_data.identity.relative_moniker,
                    configured_selectors.as_slice(),
                )
                .unwrap_or_else(|err| {
                    error!(
                        moniker = ?pumped_inspect_data.identity.relative_moniker, ?err,
                        "Failed to evaluate client selectors",
                    );
                    Vec::new()
                });

                if matching_selectors.is_empty() {
                    None
                } else {
                    match matching_selectors.try_into() {
                        Ok(hierarchy_matcher) => Some(hierarchy_matcher),
                        Err(e) => {
                            error!(?e, "Failed to create hierarchy matcher");
                            None
                        }
                    }
                }
            };

            // If there were configured matchers and none of them matched
            // this component, then we should return early since there is no data to
            // extract.
            client_selectors.as_ref()?;
        }

        let identity = pumped_inspect_data.identity.clone();

        let hierarchy_data = ReaderServer::filter_single_components_snapshot(
            pumped_inspect_data.snapshot,
            pumped_inspect_data.inspect_matcher,
            client_selectors,
            identity.to_string().as_str(),
            parent_trace_id,
        );
        Some(Data::for_inspect(
            sanitized_moniker,
            hierarchy_data.hierarchy,
            hierarchy_data.timestamp.into_nanos(),
            identity.url.clone(),
            hierarchy_data.filename,
            hierarchy_data.errors,
        ))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::collector::InspectData,
        super::*,
        crate::{
            accessor::BatchIterator,
            diagnostics::AccessorStats,
            events::{
                router::EventConsumer,
                types::{ComponentIdentifier, DiagnosticsReadyPayload, Event, EventPayload},
            },
            identity::ComponentIdentity,
            inspect::repository::InspectRepository,
            pipeline::Pipeline,
        },
        fidl::endpoints::{create_proxy_and_stream, DiscoverableProtocolMarker},
        fidl_fuchsia_diagnostics::{BatchIteratorMarker, BatchIteratorProxy, StreamMode},
        fidl_fuchsia_inspect::TreeMarker,
        fuchsia_async::{self as fasync, Task},
        fuchsia_component::server::ServiceFs,
        fuchsia_inspect::{assert_data_tree, reader, testing::AnyProperty, Inspector},
        fuchsia_zircon as zx,
        fuchsia_zircon::Peered,
        futures::future::join_all,
        futures::{FutureExt, StreamExt},
        selectors::{self, VerboseError},
        serde_json::json,
        std::path::PathBuf,
    };

    const TEST_URL: &str = "fuchsia-pkg://test";
    const BATCH_RETRIEVAL_TIMEOUT_SECONDS: i64 = 300;

    fn get_vmo(text: &[u8]) -> zx::Vmo {
        let vmo = zx::Vmo::create(4096).unwrap();
        vmo.write(text, 0).unwrap();
        vmo
    }

    #[fuchsia::test]
    async fn inspect_data_collector() {
        let path = PathBuf::from("/test-bindings");
        // Make a ServiceFs containing two files.
        // One is an inspect file, and one is not.
        let mut fs = ServiceFs::new();
        let vmo = get_vmo(b"test1");
        let vmo2 = get_vmo(b"test2");
        let vmo3 = get_vmo(b"test3");
        let vmo4 = get_vmo(b"test4");
        fs.dir("diagnostics").add_vmo_file_at("root.inspect", vmo);
        fs.dir("diagnostics").add_vmo_file_at("root_not_inspect", vmo2);
        fs.dir("diagnostics").dir("a").add_vmo_file_at("root.inspect", vmo3);
        fs.dir("diagnostics").dir("b").add_vmo_file_at("root.inspect", vmo4);
        // Create a connection to the ServiceFs.
        let (h0, h1) = fidl::endpoints::create_endpoints().unwrap();
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
            let mut executor = fasync::LocalExecutor::new().unwrap();

            executor.run_singlethreaded(async {
                let extra_data = collector::collect(path).await.expect("collector missing data");
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

    #[fuchsia::test]
    async fn inspect_data_collector_tree() {
        let path = PathBuf::from("/test-bindings2");

        // Make a ServiceFs serving an inspect tree.
        let mut fs = ServiceFs::new();
        let inspector = Inspector::new();
        inspector.root().record_int("a", 1);
        inspector.root().record_lazy_child("lazy", || {
            async move {
                let inspector = Inspector::new();
                inspector.root().record_double("b", 3.25);
                Ok(inspector)
            }
            .boxed()
        });
        inspect_runtime::serve(&inspector, &mut fs).expect("failed to serve inspector");

        // Create a connection to the ServiceFs.
        let (h0, h1) = fidl::endpoints::create_endpoints().unwrap();
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
            let mut executor = fasync::LocalExecutor::new().unwrap();

            executor.run_singlethreaded(async {
                let extra_data = collector::collect(path).await.expect("collector missing data");
                assert_eq!(1, extra_data.len());

                let extra = extra_data.get(TreeMarker::PROTOCOL_NAME);
                assert!(extra.is_some());

                match extra.unwrap() {
                    InspectData::Tree(tree) => {
                        // Assert we can read the tree proxy and get the data we expected.
                        let hierarchy =
                            reader::read(tree).await.expect("failed to read hierarchy from tree");
                        assert_data_tree!(hierarchy, root: {
                            a: 1i64,
                            lazy: {
                                b: 3.25,
                            }
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

    #[fuchsia::test]
    async fn reader_server_formatting() {
        let path = PathBuf::from("/test-bindings3");

        // Make a ServiceFs containing two files.
        // One is an inspect file, and one is not.
        let mut fs = ServiceFs::new();
        let vmo = zx::Vmo::create(4096).unwrap();
        let inspector = inspector_for_reader_test();

        let data = inspector.copy_vmo_data().unwrap();
        vmo.write(&data, 0).unwrap();
        fs.dir("diagnostics").add_vmo_file_at("test.inspect", vmo);

        // Create a connection to the ServiceFs.
        let (h0, h1) = fidl::endpoints::create_endpoints().unwrap();
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
            let mut executor = fasync::LocalExecutor::new().unwrap();
            executor.run_singlethreaded(async {
                verify_reader(path).await;
                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }

    #[fuchsia::test]
    async fn read_server_formatting_tree() {
        let path = PathBuf::from("/test-bindings4");

        // Make a ServiceFs containing two files.
        // One is an inspect file, and one is not.
        let mut fs = ServiceFs::new();
        let inspector = inspector_for_reader_test();
        inspect_runtime::serve(&inspector, &mut fs).expect("failed to serve inspector");

        // Create a connection to the ServiceFs.
        let (h0, h1) = fidl::endpoints::create_endpoints().unwrap();
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
            let mut executor = fasync::LocalExecutor::new().unwrap();
            executor.run_singlethreaded(async {
                verify_reader(path).await;
                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });
        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }

    #[fuchsia::test]
    async fn reader_server_reports_errors() {
        let path = PathBuf::from("/test-bindings-errors-01");

        // Make a ServiceFs containing something that looks like an inspect file but is not.
        let mut fs = ServiceFs::new();
        let vmo = zx::Vmo::create(4096).unwrap();
        fs.dir("diagnostics").add_vmo_file_at("test.inspect", vmo);

        // Create a connection to the ServiceFs.
        let (h0, h1) = fidl::endpoints::create_endpoints().unwrap();
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
            let mut executor = fasync::LocalExecutor::new().unwrap();
            executor.run_singlethreaded(async {
                verify_reader_with_mode(path, VerifyMode::ExpectComponentFailure).await;
                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }

    #[fuchsia::test]
    async fn three_directories_two_batches() {
        stress_test_diagnostics_repository(vec![33, 33, 33], vec![64, 35]).await;
    }

    #[fuchsia::test]
    async fn max_batch_intact_two_batches_merged() {
        stress_test_diagnostics_repository(vec![64, 63, 1], vec![64, 64]).await;
    }

    #[fuchsia::test]
    async fn sixty_four_vmos_packed_into_one_batch() {
        stress_test_diagnostics_repository([1usize; 64].to_vec(), vec![64]).await;
    }

    #[fuchsia::test]
    async fn component_with_more_than_max_batch_size_is_split_in_two() {
        stress_test_diagnostics_repository(vec![65], vec![64, 1]).await;
    }

    #[fuchsia::test]
    async fn errorful_component_doesnt_halt_iteration() {
        stress_test_diagnostics_repository(vec![64, 65, 64, 64], vec![64, 64, 64, 64, 1]).await;
    }

    #[fuchsia::test]
    async fn merge_errorful_component_into_next_batch() {
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

        let inspector = inspector_for_reader_test();

        for (directory_name, filecount) in dir_name_and_filecount.clone() {
            for i in 0..filecount {
                let vmo = inspector.duplicate_vmo().expect("Failed to duplicate vmo");

                fs.dir(directory_name.clone()).add_vmo_file_at(format!("root_{}.inspect", i), vmo);
            }
        }
        let (h0, h1) = fidl::endpoints::create_endpoints().unwrap();
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
            let mut executor = fasync::LocalExecutor::new().unwrap();

            executor.run_singlethreaded(async {
                let id_and_directory_proxy =
                    join_all(dir_name_and_filecount.iter().map(|(dir, _)| {
                        let new_async_clone = cloned_path.clone();
                        async move {
                            let full_path = new_async_clone.join(dir);
                            let proxy = collector::find_directory_proxy(&full_path).await.unwrap();
                            let unique_cid = ComponentIdentifier::Legacy {
                                instance_id: "1234".into(),
                                moniker: vec![format!("component_{}.cmx", dir)].into(),
                            };
                            (unique_cid, proxy)
                        }
                    }))
                    .await;

                let pipeline = Arc::new(Pipeline::for_test(None));
                let inspect_repo =
                    Arc::new(InspectRepository::new(vec![Arc::downgrade(&pipeline)]));

                for (cid, proxy) in id_and_directory_proxy {
                    let identity = ComponentIdentity::from_identifier_and_url(cid, TEST_URL);
                    inspect_repo
                        .clone()
                        .handle(Event {
                            timestamp: zx::Time::get_monotonic(),
                            payload: EventPayload::DiagnosticsReady(DiagnosticsReadyPayload {
                                component: identity.clone(),
                                directory: Some(proxy),
                            }),
                        })
                        .await;
                }

                let inspector = Inspector::new();
                let root = inspector.root();
                let test_archive_accessor_node = root.create_child("test_archive_accessor_node");

                let test_accessor_stats = Arc::new(AccessorStats::new(test_archive_accessor_node));
                let test_batch_iterator_stats1 =
                    Arc::new(test_accessor_stats.new_inspect_batch_iterator());

                let _result_json = read_snapshot_verify_batch_count_and_batch_size(
                    inspect_repo.clone(),
                    pipeline.clone(),
                    expected_batch_results,
                    test_batch_iterator_stats1,
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
        let child_1_1_selector =
            selectors::parse_selector::<VerboseError>(r#"*:root/child_1/*:some-int"#).unwrap();
        let child_2_selector =
            selectors::parse_selector::<VerboseError>(r#"test_component.cmx:root/child_2:*"#)
                .unwrap();

        let static_selectors_opt = Some(vec![child_1_1_selector, child_2_selector]);

        let pipeline = Arc::new(Pipeline::for_test(static_selectors_opt));
        let inspect_repo = Arc::new(InspectRepository::new(vec![Arc::downgrade(&pipeline)]));

        let out_dir_proxy = collector::find_directory_proxy(&path).await.unwrap();

        // The absolute moniker here is made up since the selector is a glob
        // selector, so any path would match.
        let component_id = ComponentIdentifier::Legacy {
            instance_id: "1234".into(),
            moniker: vec!["test_component.cmx"].into(),
        };

        let inspector = Inspector::new();
        let root = inspector.root();
        let test_archive_accessor_node = root.create_child("test_archive_accessor_node");

        assert_data_tree!(inspector, root: {test_archive_accessor_node: {}});

        let test_accessor_stats = Arc::new(AccessorStats::new(test_archive_accessor_node));

        let test_batch_iterator_stats1 = Arc::new(test_accessor_stats.new_inspect_batch_iterator());

        assert_data_tree!(inspector, root: {
            test_archive_accessor_node: {
                connections_closed: 0u64,
                connections_opened: 0u64,
                inspect: {
                    batch_iterator_connections: {
                        "0": {
                            get_next: {
                                terminal_responses: 0u64,
                                responses: 0u64,
                                requests: 0u64,
                            }
                        }
                    },
                    batch_iterator: {
                        connections_closed: 0u64,
                        connections_opened: 0u64,
                        get_next: {
                            time_usec: AnyProperty,
                            requests: 0u64,
                            responses: 0u64,
                            result_count: 0u64,
                            result_errors: 0u64,
                        }
                    },
                    reader_servers_constructed: 1u64,
                    reader_servers_destroyed: 0u64,
                    schema_truncation_count: 0u64,
                    max_snapshot_sizes_bytes: AnyProperty,
                    snapshot_schema_truncation_percentage: AnyProperty,
                },
                logs: {
                    batch_iterator_connections: {},
                    batch_iterator: {
                        connections_closed: 0u64,
                        connections_opened: 0u64,
                        get_next: {
                            requests: 0u64,
                            responses: 0u64,
                            result_count: 0u64,
                            result_errors: 0u64,
                            time_usec: AnyProperty,
                        }
                    },
                    reader_servers_constructed: 0u64,
                    reader_servers_destroyed: 0u64,
                    max_snapshot_sizes_bytes: AnyProperty,
                    snapshot_schema_truncation_percentage: AnyProperty,
                    schema_truncation_count: 0u64,
                },
                stream_diagnostics_requests: 0u64,
            },
        });

        let inspector_arc = Arc::new(inspector);

        let identity = ComponentIdentity::from_identifier_and_url(component_id, TEST_URL);
        inspect_repo
            .clone()
            .handle(Event {
                timestamp: zx::Time::get_monotonic(),
                payload: EventPayload::DiagnosticsReady(DiagnosticsReadyPayload {
                    component: identity.clone(),
                    directory: Some(out_dir_proxy),
                }),
            })
            .await;

        let expected_get_next_result_errors = match mode {
            VerifyMode::ExpectComponentFailure => 1u64,
            _ => 0u64,
        };

        {
            let result_json = read_snapshot(
                inspect_repo.clone(),
                pipeline.clone(),
                inspector_arc.clone(),
                test_batch_iterator_stats1,
            )
            .await;

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
            // which isn't running in this unit test.
            assert_data_tree!(inspector_arc.clone(), root: {
                test_archive_accessor_node: {
                    connections_closed: 0u64,
                    connections_opened: 0u64,
                    inspect: {
                        batch_iterator_connections: {},
                        batch_iterator: {
                            connections_closed: 1u64,
                            connections_opened: 1u64,
                            get_next: {
                                time_usec: AnyProperty,
                                requests: 2u64,
                                responses: 2u64,
                                result_count: 1u64,
                                result_errors: expected_get_next_result_errors,
                            }
                        },
                        component_time_usec: AnyProperty,
                        reader_servers_constructed: 1u64,
                        reader_servers_destroyed: 1u64,
                        schema_truncation_count: 0u64,
                        max_snapshot_sizes_bytes: AnyProperty,
                        snapshot_schema_truncation_percentage: AnyProperty,
                        longest_processing_times: contains {
                            "test_component.cmx": contains {
                                "@time": AnyProperty,
                                duration_seconds: AnyProperty,
                            }
                        },
                    },
                    logs: {
                        batch_iterator_connections: {},
                        batch_iterator: {
                            connections_closed: 0u64,
                            connections_opened: 0u64,
                            get_next: {
                                requests: 0u64,
                                responses: 0u64,
                                result_count: 0u64,
                                result_errors: 0u64,
                                time_usec: AnyProperty,
                            }
                        },
                        reader_servers_constructed: 0u64,
                        reader_servers_destroyed: 0u64,
                        max_snapshot_sizes_bytes: AnyProperty,
                        snapshot_schema_truncation_percentage: AnyProperty,
                        schema_truncation_count: 0u64,
                    },
                    stream_diagnostics_requests: 0u64,
                },
            });
        }

        let test_batch_iterator_stats2 = Arc::new(test_accessor_stats.new_inspect_batch_iterator());

        inspect_repo.terminate_inspect(&identity).await;
        {
            let result_json = read_snapshot(
                inspect_repo.clone(),
                pipeline.clone(),
                inspector_arc.clone(),
                test_batch_iterator_stats2,
            )
            .await;

            let result_array = result_json.as_array().expect("unit test json should be array.");
            assert_eq!(result_array.len(), 0, "Expect no schemas to be returned.");

            assert_data_tree!(inspector_arc.clone(), root: {
                test_archive_accessor_node: {
                    connections_closed: 0u64,
                    connections_opened: 0u64,
                    inspect: {
                        batch_iterator_connections: {},
                        batch_iterator: {
                            connections_closed: 2u64,
                            connections_opened: 2u64,
                            get_next: {
                                time_usec: AnyProperty,
                                requests: 3u64,
                                responses: 3u64,
                                result_count: 1u64,
                                result_errors: expected_get_next_result_errors,
                            }
                        },
                        component_time_usec: AnyProperty,
                        reader_servers_constructed: 2u64,
                        reader_servers_destroyed: 2u64,
                        schema_truncation_count: 0u64,
                        max_snapshot_sizes_bytes: AnyProperty,
                        snapshot_schema_truncation_percentage: AnyProperty,
                        longest_processing_times: contains {
                            "test_component.cmx": contains {
                                "@time": AnyProperty,
                                duration_seconds: AnyProperty,
                            }
                        },
                    },
                    logs: {
                        batch_iterator_connections: {},
                        batch_iterator: {
                            connections_closed: 0u64,
                            connections_opened: 0u64,
                            get_next: {
                                requests: 0u64,
                                responses: 0u64,
                                result_count: 0u64,
                                result_errors: 0u64,
                                time_usec: AnyProperty,
                            }
                        },
                        reader_servers_constructed: 0u64,
                        reader_servers_destroyed: 0u64,
                        max_snapshot_sizes_bytes: AnyProperty,
                        snapshot_schema_truncation_percentage: AnyProperty,
                        schema_truncation_count: 0u64,
                    },
                    stream_diagnostics_requests: 0u64,
                },
            });
        }
    }

    async fn start_snapshot(
        inspect_repo: Arc<InspectRepository>,
        pipeline: Arc<Pipeline>,
        stats: Arc<BatchIteratorConnectionStats>,
    ) -> (BatchIteratorProxy, Task<()>) {
        let test_performance_config = PerformanceConfig {
            batch_timeout_sec: BATCH_RETRIEVAL_TIMEOUT_SECONDS,
            aggregated_content_limit_bytes: None,
        };

        let trace_id = ftrace::Id::random();
        let reader_server = Box::pin(ReaderServer::stream(
            inspect_repo
                .fetch_inspect_data(&None, pipeline.read().await.static_selectors_matchers())
                .await,
            test_performance_config,
            // No selectors
            None,
            // No output rewriter
            None,
            stats.clone(),
            trace_id,
        ));
        let (consumer, batch_iterator_requests) =
            create_proxy_and_stream::<BatchIteratorMarker>().unwrap();
        (
            consumer,
            Task::spawn(async {
                BatchIterator::new(
                    reader_server,
                    batch_iterator_requests,
                    StreamMode::Snapshot,
                    stats,
                    None,
                    ftrace::Id::random(),
                )
                .unwrap()
                .run()
                .await
                .unwrap()
            }),
        )
    }

    async fn read_snapshot(
        inspect_repo: Arc<InspectRepository>,
        pipeline: Arc<Pipeline>,
        _test_inspector: Arc<Inspector>,
        stats: Arc<BatchIteratorConnectionStats>,
    ) -> serde_json::Value {
        let (consumer, server) = start_snapshot(inspect_repo, pipeline, stats).await;

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

        // ensures connection is marked as closed, wait for stream to terminate
        drop(consumer);
        server.await;

        let result_string = format!("[{}]", result_vec.join(","));
        serde_json::from_str(&result_string).unwrap_or_else(|_| {
            panic!("unit tests shouldn't be creating malformed json: {}", result_string)
        })
    }

    async fn read_snapshot_verify_batch_count_and_batch_size(
        inspect_repo: Arc<InspectRepository>,
        pipeline: Arc<Pipeline>,
        expected_batch_sizes: Vec<usize>,
        stats: Arc<BatchIteratorConnectionStats>,
    ) -> serde_json::Value {
        let (consumer, server) = start_snapshot(inspect_repo, pipeline, stats).await;

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

        // ensures connection is marked as closed, wait for stream to terminate
        drop(consumer);
        server.await;

        let result_string = format!("[{}]", result_vec.join(","));
        serde_json::from_str(&result_string).unwrap_or_else(|_| {
            panic!("unit tests shouldn't be creating malformed json: {}", result_string)
        })
    }
}
