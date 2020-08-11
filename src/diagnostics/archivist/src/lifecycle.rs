// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        constants, container::LifecycleDataContainer, diagnostics::DiagnosticsServerStats,
        formatter, repository::DiagnosticsDataRepository, server::DiagnosticsServer,
    },
    anyhow::Error,
    async_trait::async_trait,
    diagnostics_data::Data,
    fidl_fuchsia_diagnostics::{self, BatchIteratorRequestStream},
    fuchsia_inspect::NumericProperty,
    futures::stream::FusedStream,
    futures::TryStreamExt,
    parking_lot::RwLock,
    selectors,
    std::sync::Arc,
};

/// LifecycleServer holds the state and data needed to serve Lifecycle data
/// reading requests for a single client.
///
/// configured_selectors: are the selectors provided by the client which define
///                       what lifecycle event data is returned by read requests. A none type
///                       implies that all available data should be returned.
///
/// diagnostics_repo: the DiagnosticsDataRepository which holds the access-points for
///               all relevant lifecycle data.
#[derive(Clone)]
pub struct LifecycleServer {
    pub diagnostics_repo: Arc<RwLock<DiagnosticsDataRepository>>,
    pub configured_selectors: Option<Vec<Arc<fidl_fuchsia_diagnostics::Selector>>>,
    pub server_stats: Arc<DiagnosticsServerStats>,
}

impl Drop for LifecycleServer {
    fn drop(&mut self) {
        self.server_stats.global_stats.reader_servers_destroyed.add(1);
    }
}

impl LifecycleServer {
    pub fn new(
        diagnostics_repo: Arc<RwLock<DiagnosticsDataRepository>>,
        configured_selectors: Option<Vec<fidl_fuchsia_diagnostics::Selector>>,
        server_stats: Arc<DiagnosticsServerStats>,
    ) -> Self {
        server_stats.global_stats.reader_servers_constructed.add(1);
        LifecycleServer {
            diagnostics_repo,
            configured_selectors: configured_selectors.map(|selectors| {
                selectors.into_iter().map(|selector| Arc::new(selector)).collect()
            }),
            server_stats,
        }
    }

    /// Take a vector of LifecycleDataContainer structs, and a `fidl_fuchsia_diagnostics/Format`
    /// enum, and writes each container into a READ_ONLY VMO according to
    /// provided format. This VMO is then packaged into a `fidl_fuchsia_mem/Buffer`
    /// which is then packaged into a `fidl_fuchsia_diagnostics/FormattedContent`
    /// xunion which specifies the format of the VMO for clients.
    ///
    /// Errors in the returned Vector correspond to IO failures in writing to a VMO. If
    /// a node hierarchy fails to format, its vmo is an empty string.
    fn format_events(
        format: &fidl_fuchsia_diagnostics::Format,
        lifecycle_containers: Vec<LifecycleDataContainer>,
    ) -> Vec<Result<fidl_fuchsia_diagnostics::FormattedContent, Error>> {
        lifecycle_containers
            .into_iter()
            .map(|lifecycle_container| {
                let sanitized_moniker = lifecycle_container
                    .relative_moniker
                    .iter()
                    .map(|s| selectors::sanitize_string_for_selectors(s))
                    .collect::<Vec<String>>()
                    .join("/");

                let lifecycle_data = Data::for_lifecycle_event(
                    sanitized_moniker,
                    lifecycle_container.lifecycle_type,
                    lifecycle_container.payload,
                    lifecycle_container.component_url,
                    lifecycle_container.event_timestamp.into_nanos(),
                    Vec::new(),
                );
                formatter::write_schema_to_formatted_content(lifecycle_data, format)
            })
            .collect()
    }
}

#[async_trait]
impl DiagnosticsServer for LifecycleServer {
    /// Takes a BatchIterator server channel and starts serving snapshotted
    /// lifecycle events as vectors of FormattedContent. The hierarchies
    /// are served in batches of `IN_MEMORY_SNAPSHOT_LIMIT` at a time, and snapshots of
    /// diagnostics data aren't taken until a component is included in the upcoming batch.
    ///
    /// NOTE: This API does not send the terminal empty-vector at the end of the snapshot.
    async fn serve_snapshot(
        &self,
        stream: &mut BatchIteratorRequestStream,
        format: &fidl_fuchsia_diagnostics::Format,
        _diagnostics_server_stats: Arc<DiagnosticsServerStats>,
    ) -> Result<(), Error> {
        if stream.is_terminated() {
            return Ok(());
        }
        let lifecycle_data = self.diagnostics_repo.read().fetch_lifecycle_event_data();
        let lifecycle_data_length = lifecycle_data.len();
        let mut lifecycle_data_iter = lifecycle_data.into_iter();
        let mut iter = 0;
        let max = (lifecycle_data_length - 1 / constants::IN_MEMORY_SNAPSHOT_LIMIT) + 1;
        while let Some(req) = stream.try_next().await? {
            match req {
                fidl_fuchsia_diagnostics::BatchIteratorRequest::GetNext { responder } => {
                    let snapshot_batch: Vec<LifecycleDataContainer> = (&mut lifecycle_data_iter)
                        .take(constants::IN_MEMORY_SNAPSHOT_LIMIT)
                        .collect();

                    iter = iter + 1;

                    let formatted_content: Vec<
                        Result<fidl_fuchsia_diagnostics::FormattedContent, Error>,
                    > = LifecycleServer::format_events(format, snapshot_batch);

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
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            diagnostics,
            events::types::{ComponentIdentifier, LegacyIdentifier},
            inspect::collector::InspectDataCollector,
        },
        fdio,
        fidl::endpoints::{create_proxy, ServerEnd},
        fuchsia_async as fasync,
        fuchsia_component::server::ServiceFs,
        fuchsia_inspect::Inspector,
        fuchsia_zircon as zx,
        fuchsia_zircon::Peered,
        futures::StreamExt,
        std::path::PathBuf,
    };

    const TEST_URL: &'static str = "fuchsia-pkg://test";

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
        let diagnostics_repo = Arc::new(RwLock::new(DiagnosticsDataRepository::new(None)));

        let out_dir_proxy = InspectDataCollector::find_directory_proxy(&path).await.unwrap();

        // The absolute moniker here is made up since the selector is a glob
        // selector, so any path would match.
        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id: "1234".into(),
            realm_path: vec![].into(),
            component_name: "test_component.cmx".into(),
        });

        diagnostics_repo
            .write()
            .add_new_component(component_id.clone(), TEST_URL, zx::Time::from_nanos(0), None)
            .unwrap();

        diagnostics_repo
            .write()
            .add_inspect_artifacts(
                component_id.clone(),
                TEST_URL,
                out_dir_proxy,
                zx::Time::from_nanos(0),
            )
            .unwrap();

        let inspector = Inspector::new();
        let root = inspector.root();
        let test_archive_accessor_node = root.create_child("test_archive_accessor_node");

        let test_accessor_stats =
            Arc::new(diagnostics::ArchiveAccessorStats::new(test_archive_accessor_node));

        let test_batch_iterator_stats1 = Arc::new(
            diagnostics::DiagnosticsServerStats::for_lifecycle(test_accessor_stats.clone()),
        );
        {
            let reader_server = LifecycleServer::new(
                diagnostics_repo.clone(),
                None,
                test_batch_iterator_stats1.clone(),
            );

            let result_json =
                read_snapshot(reader_server, test_batch_iterator_stats1.clone()).await;

            let result_array = result_json.as_array().expect("unit test json should be array.");
            assert_eq!(result_array.len(), 2, "Expect only two schemas to be returned.");
        }

        diagnostics_repo.write().remove(&component_id);
        let test_batch_iterator_stats2 = Arc::new(
            diagnostics::DiagnosticsServerStats::for_lifecycle(test_accessor_stats.clone()),
        );

        {
            let reader_server = LifecycleServer::new(
                diagnostics_repo.clone(),
                None,
                test_batch_iterator_stats2.clone(),
            );

            let result_json =
                read_snapshot(reader_server, test_batch_iterator_stats2.clone()).await;

            let result_array = result_json.as_array().expect("unit test json should be array.");
            assert_eq!(result_array.len(), 0, "Expect no schema to be returned.");
        }
    }

    async fn read_snapshot(
        reader_server: LifecycleServer,
        server_stats: Arc<DiagnosticsServerStats>,
    ) -> serde_json::Value {
        let (consumer, batch_iterator): (
            _,
            ServerEnd<fidl_fuchsia_diagnostics::BatchIteratorMarker>,
        ) = create_proxy().unwrap();

        fasync::Task::spawn(async move {
            reader_server
                .stream_diagnostics(
                    fidl_fuchsia_diagnostics::StreamMode::Snapshot,
                    fidl_fuchsia_diagnostics::Format::Json,
                    batch_iterator,
                    server_stats.clone(),
                )
                .unwrap();
        })
        .detach();

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
