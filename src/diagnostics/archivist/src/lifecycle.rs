// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{container::LifecycleDataContainer, repository::DiagnosticsDataRepository},
    diagnostics_data::{Data, LifecycleData},
    futures::prelude::*,
    parking_lot::RwLock,
    selectors,
    std::{
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
    },
};

/// LifecycleServer holds the state and data needed to serve Lifecycle data
/// reading requests for a single client.
///
/// diagnostics_repo: the DiagnosticsDataRepository which holds the access-points for
///               all relevant lifecycle data.
pub struct LifecycleServer {
    data: std::vec::IntoIter<LifecycleDataContainer>,
}

impl LifecycleServer {
    pub fn new(diagnostics_repo: Arc<RwLock<DiagnosticsDataRepository>>) -> Self {
        LifecycleServer { data: diagnostics_repo.read().fetch_lifecycle_event_data().into_iter() }
    }

    fn next_event(&mut self) -> Option<LifecycleData> {
        self.data.next().map(|lifecycle_container| {
            let sanitized_moniker = lifecycle_container
                .relative_moniker
                .iter()
                .map(|s| selectors::sanitize_string_for_selectors(s))
                .collect::<Vec<String>>()
                .join("/");

            Data::for_lifecycle_event(
                sanitized_moniker,
                lifecycle_container.lifecycle_type,
                lifecycle_container.payload,
                lifecycle_container.component_url,
                lifecycle_container.event_timestamp.into_nanos(),
                Vec::new(),
            )
        })
    }
}

impl Stream for LifecycleServer {
    type Item = LifecycleData;

    fn poll_next(mut self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        Poll::Ready(self.next_event())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            diagnostics::{self, DiagnosticsServerStats},
            events::types::{ComponentIdentifier, LegacyIdentifier},
            inspect::collector::InspectDataCollector,
            server::AccessorServer,
        },
        fdio,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_diagnostics::BatchIteratorMarker,
        fuchsia_async::{self as fasync, Task},
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
        let diagnostics_repo = Arc::new(RwLock::new(DiagnosticsDataRepository::new(
            crate::logs::LogManager::new(),
            None,
        )));

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
            let reader_server = LifecycleServer::new(diagnostics_repo.clone());
            let result_json = read_snapshot(reader_server, test_batch_iterator_stats1).await;

            let result_array = result_json.as_array().expect("unit test json should be array.");
            assert_eq!(result_array.len(), 2, "Expect only two schemas to be returned.");
        }

        diagnostics_repo.write().remove(&component_id);
        let test_batch_iterator_stats2 = Arc::new(
            diagnostics::DiagnosticsServerStats::for_lifecycle(test_accessor_stats.clone()),
        );

        {
            let reader_server = LifecycleServer::new(diagnostics_repo.clone());
            let result_json = read_snapshot(reader_server, test_batch_iterator_stats2).await;

            let result_array = result_json.as_array().expect("unit test json should be array.");
            assert_eq!(result_array.len(), 0, "Expect no schema to be returned.");
        }
    }

    async fn read_snapshot(
        reader_server: LifecycleServer,
        stats: Arc<DiagnosticsServerStats>,
    ) -> serde_json::Value {
        let (consumer, batch_iterator_requests) =
            create_proxy_and_stream::<BatchIteratorMarker>().unwrap();
        let _server = Task::spawn(async move {
            AccessorServer::new(reader_server, batch_iterator_requests, stats)
                .unwrap()
                .run()
                .await
                .unwrap()
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
