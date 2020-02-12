// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_paver::{
        self as paver, BootManagerRequest, BootManagerRequestStream, PaverRequest,
        PaverRequestStream,
    },
    fidl_fuchsia_pkg::{PackageResolverRequestStream, PackageResolverResolveResponder},
    fidl_fuchsia_update::{
        CheckStartedResult, Initiator, ManagerMarker, ManagerProxy, ManagerState, MonitorEvent,
        MonitorEventStream, MonitorMarker, Options,
    },
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_zircon::Status,
    futures::{channel::mpsc, prelude::*},
    matches::assert_matches,
    parking_lot::Mutex,
    std::{
        collections::HashMap,
        fs::{self, create_dir, File},
        path::{Path, PathBuf},
        sync::Arc,
    },
    tempfile::TempDir,
};

mod server;
use server::OmahaReponse;

const OMAHA_CLIENT_CMX: &str =
    "fuchsia-pkg://fuchsia.com/omaha-client-integration-tests#meta/omaha-client-service.cmx";

struct Mounts {
    _test_dir: TempDir,
    config_data: PathBuf,
    packages: PathBuf,
}

impl Mounts {
    fn new() -> Self {
        let test_dir = TempDir::new().expect("create test tempdir");
        let config_data = test_dir.path().join("config_data");
        create_dir(&config_data).expect("create config_data dir");
        let packages = test_dir.path().join("packages");
        create_dir(&packages).expect("create packages dir");

        Self { _test_dir: test_dir, config_data, packages }
    }

    fn write_url(&self, url: impl AsRef<[u8]>) {
        let url_path = self.config_data.join("omaha_url");
        fs::write(url_path, url).expect("write omaha_url");
    }

    fn write_appid(&self, appid: impl AsRef<[u8]>) {
        let appid_path = self.config_data.join("omaha_app_id");
        fs::write(appid_path, appid).expect("write omaha_app_id");
    }
}
struct Proxies {
    paver: Arc<MockPaver>,
    resolver: Arc<MockResolver>,
    update_manager: ManagerProxy,
}

struct TestEnv {
    _env: NestedEnvironment,
    mounts: Mounts,
    proxies: Proxies,
    _omaha_client: App,
}

impl TestEnv {
    fn new(paver: MockPaver, response: OmahaReponse) -> Self {
        let mounts = Mounts::new();

        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_posix_socket::ProviderMarker, _>();

        let server = server::OmahaServer::new(response);
        let url = server.start().expect("start server");
        mounts.write_url(url);
        mounts.write_appid("integration-test-appid");

        let paver = Arc::new(paver);
        let paver_clone = paver.clone();
        fs.add_fidl_service(move |stream: PaverRequestStream| {
            let paver_clone = paver_clone.clone();
            fasync::spawn(paver_clone.run_service(stream));
        });

        let resolver = Arc::new(MockResolver::new());
        let resolver_clone = resolver.clone();
        fs.add_fidl_service(move |stream: PackageResolverRequestStream| {
            let resolver_clone = resolver_clone.clone();
            fasync::spawn(resolver_clone.run_resolver_service(stream))
        });

        let env = fs
            .create_salted_nested_environment("omaha_client_integration_test_env")
            .expect("nested environment to create successfully");
        fasync::spawn(fs.collect());

        let omaha_client = AppBuilder::new(OMAHA_CLIENT_CMX)
            .add_dir_to_namespace(
                "/config/data".into(),
                File::open(&mounts.config_data).expect("open config_data"),
            )
            .unwrap()
            .spawn(env.launcher())
            .expect("omaha_client to launch");

        Self {
            _env: env,
            mounts,
            proxies: Proxies {
                paver,
                resolver,
                update_manager: omaha_client
                    .connect_to_service::<ManagerMarker>()
                    .expect("connect to update manager"),
            },
            _omaha_client: omaha_client,
        }
    }

    fn register_package(&mut self, name: impl AsRef<str>, merkle: impl AsRef<str>) -> TestPackage {
        let name = name.as_ref();
        let merkle = merkle.as_ref();

        let root = self.mounts.packages.join(merkle);
        create_dir(&root).expect("package to not yet exist");

        self.proxies.resolver.mock_package_result(
            format!("fuchsia-pkg://integration.test.fuchsia.com/{}", name),
            Ok(root.clone()),
        );

        TestPackage { root }.add_file("meta", merkle)
    }

    async fn check_now(&self) -> MonitorEventStream {
        let options = Options { initiator: Some(Initiator::User) };
        let (client_proxy, server_end) = fidl::endpoints::create_proxy::<MonitorMarker>().unwrap();
        assert_eq!(
            self.proxies
                .update_manager
                .check_now(options, Some(server_end))
                .await
                .expect("check_now"),
            CheckStartedResult::Started
        );
        client_proxy.take_event_stream()
    }
}

struct TestPackage {
    root: PathBuf,
}

impl TestPackage {
    fn add_file(self, path: impl AsRef<Path>, contents: impl AsRef<[u8]>) -> Self {
        std::fs::write(self.root.join(path), contents).expect("write file");
        self
    }
}

struct MockPaver {
    response: Status,
    set_active_configuration_healthy_was_called_sender: mpsc::UnboundedSender<()>,
    set_active_configuration_healthy_was_called: Mutex<mpsc::UnboundedReceiver<()>>,
}

impl MockPaver {
    fn new(response: Status) -> Self {
        let (
            set_active_configuration_healthy_was_called_sender,
            set_active_configuration_healthy_was_called,
        ) = mpsc::unbounded();
        Self {
            response,
            set_active_configuration_healthy_was_called_sender,
            set_active_configuration_healthy_was_called: Mutex::new(
                set_active_configuration_healthy_was_called,
            ),
        }
    }
    async fn run_service(self: Arc<Self>, mut stream: PaverRequestStream) {
        while let Some(req) = stream.try_next().await.unwrap() {
            match req {
                PaverRequest::FindBootManager { boot_manager, .. } => {
                    let mock_paver_clone = self.clone();
                    fasync::spawn(
                        mock_paver_clone
                            .run_boot_manager_service(boot_manager.into_stream().unwrap()),
                    );
                }
                PaverRequest::FindDataSink { data_sink, .. } => {
                    let paver_service_clone = self.clone();
                    fasync::spawn(
                        paver_service_clone.run_data_sink_service(data_sink.into_stream().unwrap()),
                    );
                }
                req => println!("mock Paver ignoring request: {:?}", req),
            }
        }
    }
    async fn run_boot_manager_service(self: Arc<Self>, mut stream: BootManagerRequestStream) {
        while let Some(req) = stream.try_next().await.unwrap() {
            match req {
                BootManagerRequest::SetActiveConfigurationHealthy { responder } => {
                    self.set_active_configuration_healthy_was_called_sender
                        .unbounded_send(())
                        .expect("mpsc send");
                    responder.send(self.response.clone().into_raw()).expect("send ok");
                }
                req => println!("mock Paver ignoring request: {:?}", req),
            }
        }
    }
    async fn run_data_sink_service(self: Arc<Self>, mut stream: paver::DataSinkRequestStream) {
        while let Some(request) = stream.try_next().await.unwrap() {
            match request {
                paver::DataSinkRequest::WriteAsset { responder, .. } => {
                    responder.send(Status::OK.into_raw()).expect("paver response to send");
                }
                paver::DataSinkRequest::WriteBootloader { responder, .. } => {
                    responder.send(Status::OK.into_raw()).expect("paver response to send");
                }
                request => panic!("Unhandled method Paver::{}", request.method_name()),
            }
        }
    }
}

struct MockResolver {
    expectations: Mutex<HashMap<String, Result<PathBuf, Status>>>,
}

impl MockResolver {
    fn new() -> Self {
        Self { expectations: Mutex::new(HashMap::new()) }
    }
    async fn run_resolver_service(self: Arc<Self>, mut stream: PackageResolverRequestStream) {
        while let Some(event) = stream.try_next().await.unwrap() {
            match event {
                fidl_fuchsia_pkg::PackageResolverRequest::Resolve {
                    package_url,
                    selectors: _,
                    update_policy: _,
                    dir,
                    responder,
                } => self.handle_resolve(package_url, dir, responder).await,
                fidl_fuchsia_pkg::PackageResolverRequest::GetHash { .. } => {
                    panic!("GetHash not implemented")
                }
            }
        }
    }

    async fn handle_resolve(
        &self,
        package_url: String,
        dir: ServerEnd<DirectoryMarker>,
        responder: PackageResolverResolveResponder,
    ) {
        eprintln!("TEST: Got resolve request for {:?}", package_url);

        let response = self
            .expectations
            .lock()
            .get(&package_url)
            .map(|entry| entry.clone())
            .unwrap_or(Err(Status::OK));

        let response_status = match response {
            Ok(package_dir) => {
                // Open the package directory using the directory request given by the client
                // asking to resolve the package.
                fdio::service_connect(
                    package_dir.to_str().expect("path to str"),
                    dir.into_channel(),
                )
                .unwrap_or_else(|err| panic!("error connecting to tempdir {:?}", err));
                Status::OK
            }
            Err(status) => status,
        };
        responder.send(response_status.into_raw()).unwrap();
    }

    fn mock_package_result(&self, url: impl Into<String>, response: Result<PathBuf, Status>) {
        self.expectations.lock().insert(url.into(), response);
    }
}

#[fasync::run_singlethreaded(test)]
// Test will hang if omaha-client does not call paver service
async fn test_calls_paver_service() {
    let env = TestEnv::new(MockPaver::new(Status::OK), OmahaReponse::NoUpdate);

    let mut set_active_configuration_healthy_was_called =
        env.proxies.paver.set_active_configuration_healthy_was_called.lock();
    assert_eq!(set_active_configuration_healthy_was_called.next().await, Some(()));
}

#[fasync::run_singlethreaded(test)]
// Test will hang if omaha-client does not call paver service
async fn test_update_manager_get_state_works_after_paver_service_fails() {
    let env = TestEnv::new(MockPaver::new(Status::INTERNAL), OmahaReponse::NoUpdate);

    let mut set_active_configuration_healthy_was_called =
        env.proxies.paver.set_active_configuration_healthy_was_called.lock();
    set_active_configuration_healthy_was_called.next().await;

    let state = env.proxies.update_manager.get_state().await.expect("get_state");
    assert_eq!(state.state, Some(ManagerState::Idle));
    assert_eq!(state.version_available, None);
}

async fn expect_states(stream: &mut MonitorEventStream, expected_states: &[ManagerState]) {
    for &expected_state in expected_states {
        let MonitorEvent::OnState { state } = stream.try_next().await.unwrap().unwrap();
        assert_eq!(state.state, Some(expected_state));
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_update() {
    let mut env = TestEnv::new(MockPaver::new(Status::INTERNAL), OmahaReponse::Update);

    env.register_package(
        "update?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
        "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
    )
    .add_file(
        "packages",
        "system_image/0=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead\n",
    )
    .add_file("zbi", "fake zbi");

    let mut stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[
            ManagerState::Idle,
            ManagerState::CheckingForUpdates,
            ManagerState::UpdateAvailable,
            ManagerState::PerformingUpdate,
            ManagerState::WaitingForReboot,
        ],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_update_error() {
    let env = TestEnv::new(MockPaver::new(Status::INTERNAL), OmahaReponse::Update);

    let mut stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[
            ManagerState::Idle,
            ManagerState::CheckingForUpdates,
            ManagerState::UpdateAvailable,
            ManagerState::PerformingUpdate,
            ManagerState::EncounteredError,
            ManagerState::Idle,
        ],
    )
    .await;
    assert_matches!(stream.next().await, None);
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_no_update() {
    let env = TestEnv::new(MockPaver::new(Status::INTERNAL), OmahaReponse::NoUpdate);

    let mut stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[ManagerState::Idle, ManagerState::CheckingForUpdates, ManagerState::Idle],
    )
    .await;
    assert_matches!(stream.next().await, None);
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_invalid_response() {
    let env = TestEnv::new(MockPaver::new(Status::INTERNAL), OmahaReponse::InvalidResponse);

    let mut stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[
            ManagerState::Idle,
            ManagerState::CheckingForUpdates,
            ManagerState::EncounteredError,
            ManagerState::Idle,
        ],
    )
    .await;
    assert_matches!(stream.next().await, None);
}

#[fasync::run_singlethreaded(test)]
async fn test_omaha_client_invalid_url() {
    let env = TestEnv::new(MockPaver::new(Status::INTERNAL), OmahaReponse::InvalidURL);

    let mut stream = env.check_now().await;
    expect_states(
        &mut stream,
        &[
            ManagerState::Idle,
            ManagerState::CheckingForUpdates,
            ManagerState::UpdateAvailable,
            ManagerState::EncounteredError,
            ManagerState::Idle,
        ],
    )
    .await;
    assert_matches!(stream.next().await, None);
}
