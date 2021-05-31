// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{diagnostics::IsolatedLogsProvider, error::*},
    anyhow::Error,
    cm_rust,
    diagnostics_bridge::ArchiveReaderManager,
    fdiagnostics::ArchiveAccessorProxy,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_diagnostics as fdiagnostics, fidl_fuchsia_io2 as fio2, fidl_fuchsia_test as ftest,
    fidl_fuchsia_test_internal as ftest_internal, fidl_fuchsia_test_manager as ftest_manager,
    ftest::SuiteMarker,
    ftest_manager::{LaunchError, LaunchOptions, SuiteControllerRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{
        builder::{
            Capability, CapabilityRoute, ComponentSource, Event, RealmBuilder, RouteEndpoint,
        },
        error::Error as RealmBuilderError,
        mock::{Mock, MockHandles},
        Realm, RealmInstance,
    },
    fuchsia_zircon as zx,
    futures::prelude::*,
    lazy_static::lazy_static,
    regex::Regex,
    std::collections::HashMap,
    std::sync::{Arc, Mutex, Weak},
    tracing::{error, warn},
};

mod diagnostics;
mod error;

pub const TEST_ROOT_REALM_NAME: &'static str = "test_root";
pub const WRAPPER_ROOT_REALM_PATH: &'static str = "test_wrapper/test_root";
pub const ARCHIVIST_REALM_PATH: &'static str = "test_wrapper/archivist";

lazy_static! {
    static ref ARCHIVIST_FOR_EMBEDDING_URL: &'static str =
        "fuchsia-pkg://fuchsia.com/test_manager#meta/archivist-for-embedding.cm";
    static ref READ_RIGHTS: fio2::Operations = fio2::Operations::Connect
        | fio2::Operations::Enumerate
        | fio2::Operations::Traverse
        | fio2::Operations::ReadBytes
        | fio2::Operations::GetAttributes;
    static ref READ_WRITE_RIGHTS: fio2::Operations = fio2::Operations::Connect
        | fio2::Operations::Enumerate
        | fio2::Operations::Traverse
        | fio2::Operations::ReadBytes
        | fio2::Operations::WriteBytes
        | fio2::Operations::ModifyDirectory
        | fio2::Operations::GetAttributes
        | fio2::Operations::UpdateAttributes;
    static ref ADMIN_RIGHTS: fio2::Operations = fio2::Operations::Admin;
}

struct TestMapValue {
    test_url: String,
    can_be_deleted: bool,
    last_accessed: fasync::Time,
}

/// Cache mapping test realm name to test url.
/// This cache will run cleanup on constant intervals and delete entries which have been marked as
/// stale and not accessed for `cleanup_interval` duration.
/// We don't delete entries as soon as they are marked as stale because dependent
/// service might still be processing requests.
pub struct TestMap {
    /// Key: test realm name
    test_map: Mutex<HashMap<String, TestMapValue>>,

    /// Interval after which cleanup is fired.
    cleanup_interval: zx::Duration,
}

impl TestMap {
    /// Create new instance of this object, wrap it in Arc and return.
    /// 'cleanup_interval': Intervals after which cleanup should fire.
    pub fn new(cleanup_interval: zx::Duration) -> Arc<Self> {
        let s = Arc::new(Self { test_map: Mutex::new(HashMap::new()), cleanup_interval });
        let weak = Arc::downgrade(&s);
        let d = s.cleanup_interval.clone();
        fasync::Task::spawn(async move {
            let mut interval = fasync::Interval::new(d);
            while let Some(_) = interval.next().await {
                if let Some(s) = weak.upgrade() {
                    s.run_cleanup();
                } else {
                    break;
                }
            }
        })
        .detach();
        s
    }

    fn run_cleanup(&self) {
        let mut test_map = self.test_map.lock().unwrap();
        test_map.retain(|_, v| {
            !(v.can_be_deleted && (v.last_accessed < fasync::Time::now() - self.cleanup_interval))
        });
    }

    /// Insert into the cache. If key was already present will return old value.
    pub fn insert(&self, test_name: String, test_url: String) -> Option<String> {
        let mut test_map = self.test_map.lock().unwrap();
        test_map
            .insert(
                test_name,
                TestMapValue {
                    test_url,
                    can_be_deleted: false,
                    last_accessed: fasync::Time::now(),
                },
            )
            .map(|v| v.test_url)
    }

    /// Get `test_url` if present in the map.
    pub fn get(&self, k: &str) -> Option<String> {
        let mut test_map = self.test_map.lock().unwrap();
        match test_map.get_mut(k) {
            Some(v) => {
                v.last_accessed = fasync::Time::now();
                return Some(v.test_url.clone());
            }
            None => {
                return None;
            }
        }
    }

    /// Delete cache entry without marking it as stale and waiting for cleanup.
    pub fn delete(&self, k: &str) {
        let mut test_map = self.test_map.lock().unwrap();
        test_map.remove(k);
    }

    /// Mark cache entry as stale which would be deleted in future if not accessed.
    pub fn mark_as_stale(&self, k: &str) {
        let mut test_map = self.test_map.lock().unwrap();
        if let Some(v) = test_map.get_mut(k) {
            v.can_be_deleted = true;
        }
    }
}

/// Start test manager and serve it over `stream`.
pub async fn run_test_manager(
    mut stream: ftest_manager::HarnessRequestStream,
    test_map: Arc<TestMap>,
) -> Result<(), TestManagerError> {
    while let Some(event) = stream.try_next().await.map_err(TestManagerError::Stream)? {
        match event {
            ftest_manager::HarnessRequest::LaunchSuite {
                test_url,
                options,
                suite,
                controller,
                responder,
            } => {
                let controller = match controller.into_stream() {
                    Err(error) => {
                        error!(%error, component_url = %test_url, "invalid controller channel");
                        responder
                            .send(&mut Err(LaunchError::InvalidArgs))
                            .map_err(TestManagerError::Response)?;
                        // process next request
                        continue;
                    }
                    Ok(c) => c,
                };

                match launch_test(&test_url, suite, test_map.clone(), options).await {
                    Ok(test) => {
                        let test_name = test.instance.root.child_name();
                        responder.send(&mut Ok(())).map_err(TestManagerError::Response)?;
                        let test_map = test_map.clone();
                        fasync::Task::spawn(async move {
                            test.serve_controller(controller).await.unwrap_or_else(|error| {
                                error!(%error, component_url = %test_url, "serve_controller failed");
                            });
                            test_map.mark_as_stale(&test_name);
                        })
                        .detach();
                    }
                    Err(err) => {
                        error!(?err, "Failed to launch test");
                        responder.send(&mut Err(err.into())).map_err(TestManagerError::Response)?;
                    }
                }
            }
        }
    }
    Ok(())
}

/// Start test manager info server and serve it over `stream`.
pub async fn run_test_manager_info_server(
    mut stream: ftest_internal::InfoRequestStream,
    test_map: Arc<TestMap>,
) -> Result<(), TestManagerError> {
    // This ensures all monikers are relative to test_manager and supports capturing the top-level
    // name of the test realm.
    let re = Regex::new(r"^\./tests:(.*?):.*$").unwrap();
    while let Some(event) = stream.try_next().await.map_err(TestManagerError::Stream)? {
        match event {
            ftest_internal::InfoRequest::GetTestUrl { moniker, responder } => {
                if !re.is_match(&moniker) {
                    responder
                        .send(&mut Err(zx::sys::ZX_ERR_NOT_SUPPORTED))
                        .map_err(TestManagerError::Response)?;
                    continue;
                }

                let cap = re.captures(&moniker).unwrap();
                if let Some(s) = test_map.get(&cap[1]) {
                    responder.send(&mut Ok(s)).map_err(TestManagerError::Response)?;
                } else {
                    responder
                        .send(&mut Err(zx::sys::ZX_ERR_NOT_FOUND))
                        .map_err(TestManagerError::Response)?;
                }
            }
        }
    }
    Ok(())
}

struct RunningTest {
    instance: RealmInstance,
    logs_iterator_task: Option<fasync::Task<Result<(), anyhow::Error>>>,

    // safe keep archive accessor which tests might use.
    archive_accessor: Arc<ArchiveAccessorProxy>,
}

impl RunningTest {
    async fn destroy(mut self) {
        let destroy_waiter = self.instance.root.take_destroy_waiter();
        drop(self.instance);
        // When serving logs over ArchiveIterator in the host, we should also wait for all logs to
        // be drained.
        drop(self.archive_accessor);
        if let Some(task) = self.logs_iterator_task {
            task.await.unwrap_or_else(|err| {
                error!(?err, "Failed to await for logs streaming task");
            });
        }

        destroy_waiter.await.unwrap_or_else(|err| {
            error!(?err, "Failed to destroy instance");
        });
    }

    /// Serves Suite controller and destroys this test afterwards.
    pub async fn serve_controller(
        self,
        mut stream: SuiteControllerRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) = stream.try_next().await? {
            match event {
                ftest_manager::SuiteControllerRequest::Kill { .. } => {
                    self.destroy().await;
                    return Ok(());
                }
            }
        }

        self.destroy().await;
        Ok(())
    }
}

/// Launch test and return the name of test used to launch it in collection.
async fn launch_test(
    test_url: &str,
    suite_request: ServerEnd<SuiteMarker>,
    test_map: Arc<TestMap>,
    options: LaunchOptions,
) -> Result<RunningTest, LaunchTestError> {
    // This archive accessor will be served by the embedded archivist.
    let (archive_accessor, archive_accessor_server_end) =
        fidl::endpoints::create_proxy::<fdiagnostics::ArchiveAccessorMarker>()
            .map_err(LaunchTestError::CreateProxyForArchiveAccessor)?;

    let archive_accessor_arc = Arc::new(archive_accessor);
    let mut realm = get_realm(Arc::downgrade(&archive_accessor_arc), test_url)
        .await
        .map_err(LaunchTestError::InitializeTestRealm)?;
    realm.set_collection_name("tests");
    let instance = realm.create().await.map_err(LaunchTestError::CreateTestRealm)?;
    let test_name = instance.root.child_name();
    test_map.insert(test_name.clone(), test_url.to_string());
    let archive_accessor_arc_clone = archive_accessor_arc.clone();
    let connect_to_instance_services = async move {
        instance
            .root
            .connect_request_to_protocol_at_exposed_dir::<fdiagnostics::ArchiveAccessorMarker>(
                archive_accessor_server_end,
            )
            .map_err(LaunchTestError::ConnectToArchiveAccessor)?;

        let mut isolated_logs_provider = IsolatedLogsProvider::new(archive_accessor_arc_clone);
        let logs_iterator_task = match options.logs_iterator {
            None => None,
            Some(ftest_manager::LogsIterator::Archive(iterator)) => {
                let task = isolated_logs_provider
                    .spawn_iterator_server(iterator)
                    .map_err(LaunchTestError::StreamIsolatedLogs)?;
                Some(task)
            }
            Some(ftest_manager::LogsIterator::Batch(iterator)) => {
                isolated_logs_provider
                    .start_streaming_logs(iterator)
                    .map_err(LaunchTestError::StreamIsolatedLogs)?;
                None
            }
            Some(_) => None,
        };

        instance
            .root
            .connect_request_to_protocol_at_exposed_dir(suite_request)
            .map_err(LaunchTestError::ConnectToTestSuite)?;
        Ok(RunningTest { instance, logs_iterator_task, archive_accessor: archive_accessor_arc })
    };

    let running_test_result = connect_to_instance_services.await;
    if running_test_result.is_err() {
        test_map.delete(&test_name);
    }
    running_test_result
}

async fn get_realm(
    archive_accessor: Weak<fdiagnostics::ArchiveAccessorProxy>,
    test_url: &str,
) -> Result<Realm, RealmBuilderError> {
    let mut builder = RealmBuilder::new().await?;
    builder
        .add_eager_component(WRAPPER_ROOT_REALM_PATH, ComponentSource::url(test_url))
        .await?
        .add_component(
            "mocks-server",
            ComponentSource::Mock(Mock::new(move |mock_handles| {
                Box::pin(serve_mocks(archive_accessor.clone(), mock_handles))
            })),
        )
        .await?
        .add_eager_component(
            ARCHIVIST_REALM_PATH,
            ComponentSource::url(*ARCHIVIST_FOR_EMBEDDING_URL),
        )
        .await?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.process.Launcher"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.boot.WriteOnlyLog"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.sys2.EventSource"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![
                RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH),
                RouteEndpoint::component(ARCHIVIST_REALM_PATH),
            ],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::storage("temp", "/tmp"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::storage("data", "/data"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::storage("cache", "/cache"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(ARCHIVIST_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::component(ARCHIVIST_REALM_PATH),
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.Log"),
            source: RouteEndpoint::component(ARCHIVIST_REALM_PATH),
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.diagnostics.ArchiveAccessor"),
            source: RouteEndpoint::component("mocks-server"),
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.diagnostics.ArchiveAccessor"),
            source: RouteEndpoint::component(ARCHIVIST_REALM_PATH),
            targets: vec![RouteEndpoint::AboveRoot],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(Event::Started, cm_rust::EventMode::Async),
            source: RouteEndpoint::component("test_wrapper"),
            targets: vec![RouteEndpoint::component(ARCHIVIST_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(Event::Stopped, cm_rust::EventMode::Async),
            source: RouteEndpoint::component("test_wrapper"),
            targets: vec![RouteEndpoint::component(ARCHIVIST_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(Event::Running, cm_rust::EventMode::Async),
            source: RouteEndpoint::component("test_wrapper"),
            targets: vec![RouteEndpoint::component(ARCHIVIST_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(
                Event::directory_ready("diagnostics"),
                cm_rust::EventMode::Async,
            ),
            source: RouteEndpoint::component("test_wrapper"),
            targets: vec![RouteEndpoint::component(ARCHIVIST_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(
                Event::capability_requested("fuchsia.logger.LogSink"),
                cm_rust::EventMode::Async,
            ),
            source: RouteEndpoint::component("test_wrapper"),
            targets: vec![RouteEndpoint::component(ARCHIVIST_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.test.Suite"),
            source: RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH),
            targets: vec![RouteEndpoint::AboveRoot],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.hardware.display.Provider"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.scheduler.ProfileProvider"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.sysmem.Allocator"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.tracing.provider.Registry"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory("root-ssl-certificates", "", *READ_RIGHTS),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory("config-data", "", *READ_RIGHTS),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory(
                "deprecated-tmp",
                "",
                *ADMIN_RIGHTS | *READ_WRITE_RIGHTS,
            ),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory("dev-input-report", "", *READ_WRITE_RIGHTS),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory("dev-display-controller", "", *READ_WRITE_RIGHTS),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory("dev-goldfish-address-space", "", *READ_WRITE_RIGHTS),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory("dev-goldfish-control", "", *READ_WRITE_RIGHTS),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory("dev-goldfish-pipe", "", *READ_WRITE_RIGHTS),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory("dev-goldfish-sync", "", *READ_WRITE_RIGHTS),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory("dev-gpu", "", *READ_WRITE_RIGHTS),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.vulkan.loader.Loader"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?;

    Ok(builder.build())
}

async fn serve_mocks(
    archive_accessor: Weak<fdiagnostics::ArchiveAccessorProxy>,
    mock_handles: MockHandles,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        let archive_accessor_clone = archive_accessor.clone();
        fasync::Task::spawn(async move {
            diagnostics::run_intermediary_archive_accessor(archive_accessor_clone, stream)
                .await
                .unwrap_or_else(|e| {
                    warn!("Couldn't run proxied ArchiveAccessor: {:?}", e);
                })
        })
        .detach()
    });
    fs.serve_connection(mock_handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*, fasync::pin_mut, fidl::endpoints::create_proxy_and_stream,
        ftest_internal::InfoMarker, std::ops::Add, zx::DurationNum,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_info_server() {
        let (proxy, stream) = create_proxy_and_stream::<InfoMarker>().unwrap();
        let test_map = TestMap::new(10.seconds());
        let test_map_clone = test_map.clone();
        fasync::Task::local(async move {
            run_test_manager_info_server(stream, test_map_clone).await.unwrap()
        })
        .detach();
        test_map.insert("my_test".into(), "my_test_url".into());
        assert_eq!(
            proxy.get_test_url("./tests:not_available_realm:0/test_wrapper").await.unwrap(),
            Err(zx::sys::ZX_ERR_NOT_FOUND)
        );
        assert_eq!(
            proxy.get_test_url("./tests:my_test:0/test_wrapper").await.unwrap(),
            Ok("my_test_url".into())
        );
        assert_eq!(
            proxy.get_test_url("./tests:my_test:0/test_wrapper:0/my_component:0").await.unwrap(),
            Ok("my_test_url".into())
        );
        assert_eq!(
            proxy.get_test_url("./tests/my_test:0/test_wrapper:0/my_component:0").await.unwrap(),
            Err(zx::sys::ZX_ERR_NOT_SUPPORTED)
        );
        assert_eq!(
            proxy.get_test_url("/tests:my_test:0/test_wrapper:0/my_component:0").await.unwrap(),
            Err(zx::sys::ZX_ERR_NOT_SUPPORTED)
        );
    }

    async fn dummy_fn() {}

    #[test]
    fn test_map_works() {
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();
        let test_map = TestMap::new(zx::Duration::from_seconds(10));

        test_map.insert("my_test".into(), "my_test_url".into());
        assert_eq!(test_map.get("my_test"), Some("my_test_url".into()));
        assert_eq!(test_map.get("my_non_existent_test"), None);

        // entry should not be deleted until it is marked as stale.
        executor.set_fake_time(executor.now().add(12.seconds()));
        executor.wake_next_timer();
        let fut = dummy_fn();
        pin_mut!(fut);
        let _poll = executor.run_until_stalled(&mut fut);
        assert_eq!(test_map.get("my_test"), Some("my_test_url".into()));

        // only entry which was marked as stale should be deleted.
        test_map.insert("other_test".into(), "other_test_url".into());
        test_map.mark_as_stale("my_test");
        executor.set_fake_time(executor.now().add(12.seconds()));
        executor.wake_next_timer();
        let fut = dummy_fn();
        pin_mut!(fut);
        let _poll = executor.run_until_stalled(&mut fut);
        assert_eq!(test_map.get("my_test"), None);
        assert_eq!(test_map.get("other_test"), Some("other_test_url".into()));

        // entry should stay in cache for 10 seconds after marking it as stale.
        executor.set_fake_time(executor.now().add(5.seconds()));
        test_map.mark_as_stale("other_test");
        executor.set_fake_time(executor.now().add(5.seconds()));
        executor.wake_next_timer();
        let fut = dummy_fn();
        pin_mut!(fut);
        let _poll = executor.run_until_stalled(&mut fut);
        assert_eq!(test_map.get("other_test"), Some("other_test_url".into()));

        // It has been marked as stale for 10 sec now, so can be deleted.
        executor.set_fake_time(executor.now().add(11.seconds()));
        executor.wake_next_timer();
        let fut = dummy_fn();
        pin_mut!(fut);
        let _poll = executor.run_until_stalled(&mut fut);
        assert_eq!(test_map.get("other_test"), None);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_map_delete() {
        let test_map = TestMap::new(zx::Duration::from_seconds(10));
        test_map.insert("my_test".into(), "my_test_url".into());
        assert_eq!(test_map.get("my_test"), Some("my_test_url".into()));
        test_map.insert("other_test".into(), "other_test_url".into());
        test_map.delete("my_test");
        assert_eq!(test_map.get("my_test"), None);
        assert_eq!(test_map.get("other_test"), Some("other_test_url".into()));
    }
}
