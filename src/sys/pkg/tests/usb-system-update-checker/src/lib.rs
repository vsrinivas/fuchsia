// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_pkg::{PackageResolverMarker, PackageUrl},
    fidl_fuchsia_update_installer::{InstallerRequestStream, State, *},
    fidl_fuchsia_update_usb::{
        CheckError, CheckSuccess, CheckerMarker, CheckerProxy, MonitorMarker, MonitorRequest,
    },
    fuchsia_async::{self as fasync, Task},
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_pkg_testing::{PackageBuilder, RepositoryBuilder},
    fuchsia_url::pkg_url::PkgUrl,
    futures::{lock::Mutex, prelude::*},
    isolated_swd::{
        resolver::for_tests::ResolverForTest, updater::for_tests::generate_packages_json,
    },
    std::sync::Arc,
    update_package::SystemVersion,
    version::Version as SemanticVersion,
};

const REPO_URL: &str = "fuchsia-pkg://fuchsia.com";
const RESOLVER_URL: &str =
    "fuchsia-pkg://fuchsia.com/usb-system-update-checker-integration-tests#meta/pkg-resolver.cmx";
const CACHE_URL: &str =
    "fuchsia-pkg://fuchsia.com/usb-system-update-checker-integration-tests#meta/pkg-cache.cmx";
const CHECKER_URL: &str = "fuchsia-pkg://fuchsia.com/usb-system-update-checker-integration-tests#meta/usb-system-update-checker-isolated.cmx";
const UPDATE_PACKAGE_URL: &str = "fuchsia-pkg://fuchsia.com/update";

struct TestEnvBuilder {
    update_version: SystemVersion,
    system_version: Option<SystemVersion>,
    states: Vec<State>,
}

impl TestEnvBuilder {
    pub fn new() -> Self {
        TestEnvBuilder {
            update_version: SystemVersion::Opaque("".to_string()),
            system_version: None,
            states: vec![],
        }
    }

    /// Set the value of the version of the update package.
    pub fn update_version(mut self, version: SystemVersion) -> Self {
        self.update_version = version;
        self
    }

    /// Set the value of /config/build-info/version.
    pub fn system_version(mut self, version: SystemVersion) -> Self {
        self.system_version = Some(version);
        self
    }

    /// Add a state to the list of states returned by the monitor.
    pub fn state(mut self, state: State) -> Self {
        self.states.push(state);
        self
    }

    pub async fn build(self) -> TestEnv {
        let update = PackageBuilder::new("update")
            .add_resource_at("packages.json", generate_packages_json(&vec![], REPO_URL).as_bytes())
            .add_resource_at("version", self.update_version.to_string().as_bytes());

        let update = update.build().await.expect("Building update package to succeed");

        let repo = Arc::new(
            RepositoryBuilder::new()
                .add_package(update)
                .build()
                .await
                .expect("Building repository to succeed"),
        );

        let resolver = ResolverForTest::new_with_component(
            repo,
            REPO_URL.parse().unwrap(),
            None,
            RESOLVER_URL,
            CACHE_URL,
        )
        .await
        .expect("Creating resolver to succeed");

        TestEnv::new(resolver, self.system_version, self.states).await
    }
}

struct MockInstaller {
    states: Mutex<Vec<State>>,
}

impl MockInstaller {
    pub fn new(states: Vec<State>) -> Arc<Self> {
        Arc::new(MockInstaller { states: Mutex::new(states) })
    }

    async fn serve_installer(self: Arc<Self>, mut stream: InstallerRequestStream) {
        while let Some(request) = stream.try_next().await.expect("try_next() succeeds") {
            match request {
                InstallerRequest::StartUpdate {
                    url,
                    options,
                    monitor,
                    reboot_controller,
                    responder,
                } => {
                    let package_url =
                        PkgUrl::parse(&url.url).expect("Installer is passed a valid URL");
                    assert!(
                        package_url.package_hash().is_some(),
                        "Package url {} should have a hash!",
                        package_url
                    );
                    assert_eq!(options.initiator, Some(Initiator::User));
                    assert_eq!(options.allow_attach_to_existing_attempt, Some(false));
                    assert_eq!(options.should_write_recovery, Some(true));
                    responder
                        .send(&mut Ok("01234567-89ab-cdef-0123-456789abcdef".to_owned()))
                        .expect("Send response succeeds");

                    // The usb update checker is expected to detach from the reboot controller.
                    self.wait_detach(
                        reboot_controller
                            .expect("Checker should have passed a reboot controller")
                            .into_stream()
                            .expect("into_stream succeeds"),
                    )
                    .await;

                    let monitor = monitor.into_proxy().expect("into_proxy succeeds");

                    for state in self.states.lock().await.iter_mut() {
                        monitor.on_state(state).await.expect("Sending state update succeeds");
                    }
                }
                r => panic!("Unexpected installer request: {:?}", r),
            }
        }
    }

    async fn wait_detach(&self, mut stream: RebootControllerRequestStream) {
        while let Some(request) = stream.try_next().await.expect("try_next() succeeds") {
            match request {
                RebootControllerRequest::Detach { .. } => return,
                r => panic!("Unexpected reboot controller request: {:?}", r),
            }
        }
    }
}

struct TestEnv {
    _resolver: ResolverForTest,
    checker: App,
    _env: NestedEnvironment,
    _svcfs_task: Task<()>,
    monitor_task: Task<bool>,
    monitor_client: Option<fidl::endpoints::ClientEnd<MonitorMarker>>,
    _installer: Arc<MockInstaller>,
}

impl TestEnv {
    pub async fn new(
        resolver: ResolverForTest,
        system_version: Option<SystemVersion>,
        installer_states: Vec<State>,
    ) -> Self {
        let installer = MockInstaller::new(installer_states);
        let clone = installer.clone();
        let mut svcfs = ServiceFs::new();
        svcfs.add_proxy_service_to::<PackageResolverMarker, _>(
            resolver.resolver.directory_request(),
        );
        svcfs.add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, _>();
        svcfs.add_fidl_service(move |stream: InstallerRequestStream| {
            Task::spawn(clone.clone().serve_installer(stream)).detach()
        });

        let build_info_dir = tempfile::tempdir().expect("Creating temporary directory succeeds");

        if let Some(version) = system_version {
            std::fs::write(
                format!("{}/version", build_info_dir.path().to_string_lossy()),
                version.to_string(),
            )
            .expect("Writing system version succeeds");
        }

        let env = svcfs
            .create_salted_nested_environment("usb-update-checker-test")
            .expect("Creating environment succeeds");

        let checker = AppBuilder::new(CHECKER_URL)
            .add_dir_to_namespace(
                "/config/build-info".to_owned(),
                std::fs::File::open(build_info_dir.into_path()).expect("open temp dir succeeds"),
            )
            .expect("Add directory OK");

        let svcfs_task = fasync::Task::spawn(svcfs.collect());
        let checker = checker.spawn(env.launcher()).expect("Launching update checker succeeds");

        let (monitor_client, mut stream) =
            fidl::endpoints::create_request_stream::<MonitorMarker>()
                .expect("Creating monitor endpoints succeeds");
        let monitor_task = fasync::Task::spawn(async move {
            while let Some(request) = stream.try_next().await.expect("try_next succeeds") {
                match request {
                    MonitorRequest::OnUpdateStarted { .. } => {
                        return true;
                    }
                }
            }
            return false;
        });

        TestEnv {
            _resolver: resolver,
            checker,
            _env: env,
            _svcfs_task: svcfs_task,
            monitor_task,
            monitor_client: Some(monitor_client),
            _installer: installer,
        }
    }

    pub fn connect(&self) -> CheckerProxy {
        self.checker.connect_to_service::<CheckerMarker>().expect("Connect to checker succeeds")
    }

    /// Get the monitor client end that can be passed to fuchsia.update.usb/Check.
    /// Should only be called once per TestEnv.
    pub fn get_monitor_client(&mut self) -> fidl::endpoints::ClientEnd<MonitorMarker> {
        self.monitor_client.take().expect("get_monitor_client is only called once")
    }

    /// Returns true if the monitor received an OnUpdateStarted event.
    /// Blocks until the monitor either receives an event or is closed.
    pub async fn was_update_started(&mut self) -> bool {
        (&mut self.monitor_task).await
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_install_update_succeeds() {
    let mut env = TestEnvBuilder::new()
        .update_version(SystemVersion::Semantic(SemanticVersion::from([1])))
        .system_version(SystemVersion::Semantic(SemanticVersion::from([0, 1])))
        .state(State::Prepare(PrepareData::empty()))
        .state(State::Fetch(FetchData::empty()))
        .state(State::Stage(StageData::empty()))
        .state(State::WaitToReboot(WaitToRebootData::empty()))
        .state(State::DeferReboot(DeferRebootData::empty()))
        .build()
        .await;

    let proxy = env.connect();
    let result = proxy
        .check(
            &mut PackageUrl { url: UPDATE_PACKAGE_URL.to_string() },
            None,
            Some(env.get_monitor_client()),
        )
        .await
        .expect("Send request OK");

    assert_eq!(result, Ok(CheckSuccess::UpdatePerformed));
    assert!(env.was_update_started().await);
}

#[fasync::run_singlethreaded(test)]
async fn test_invalid_url_fails() {
    let env = TestEnvBuilder::new().build().await;

    let proxy = env.connect();
    let result = proxy
        .check(&mut PackageUrl { url: "wow:// this isn't even a URL!!".to_string() }, None, None)
        .await
        .expect("Send request OK");

    assert_eq!(result, Err(CheckError::InvalidUpdateUrl));
}

#[fasync::run_singlethreaded(test)]
async fn test_pinned_url_fails() {
    let env = TestEnvBuilder::new().build().await;

    let proxy = env.connect();
    // A valid hash is 64 bytes.
    let hash = "d00dfeed".repeat(8);
    let evil_url = PkgUrl::parse(&format!("fuchsia-pkg://fuchsia.com/update?hash={}", hash))
        .expect("Parsing URL succeeds");
    let result = proxy
        .check(&mut PackageUrl { url: evil_url.to_string() }, None, None)
        .await
        .expect("Send request OK");

    assert_eq!(result, Err(CheckError::InvalidUpdateUrl));
}

fn semantic_version(a: u32, b: u32, c: u32, d: u32) -> SystemVersion {
    SystemVersion::Semantic(SemanticVersion::from([a, b, c, d]))
}

#[fasync::run_singlethreaded(test)]
async fn test_version_comparison_update_older_ignored() {
    let mut env = TestEnvBuilder::new()
        .system_version(semantic_version(0, 2020_01_01, 2, 4))
        .update_version(semantic_version(0, 2019_01_01, 1, 1))
        .build()
        .await;

    let result = env
        .connect()
        .check(
            &mut PackageUrl { url: UPDATE_PACKAGE_URL.to_string() },
            None,
            Some(env.get_monitor_client()),
        )
        .await
        .expect("Send request OK");

    assert_eq!(result, Ok(CheckSuccess::UpdateNotNeeded));
    assert_eq!(env.was_update_started().await, false);
}

#[fasync::run_singlethreaded(test)]
async fn test_version_comparison_update_newer_installed() {
    let env = TestEnvBuilder::new()
        .system_version(semantic_version(0, 2020_01_01, 2, 4))
        .update_version(semantic_version(0, 2021_01_01, 1, 1))
        .state(State::Prepare(PrepareData::empty()))
        .state(State::Fetch(FetchData::empty()))
        .state(State::Stage(StageData::empty()))
        .state(State::WaitToReboot(WaitToRebootData::empty()))
        .state(State::DeferReboot(DeferRebootData::empty()))
        .build()
        .await;

    let result = env
        .connect()
        .check(&mut PackageUrl { url: UPDATE_PACKAGE_URL.to_string() }, None, None)
        .await
        .expect("Send request OK");

    assert_eq!(result, Ok(CheckSuccess::UpdatePerformed));
}

#[fasync::run_singlethreaded(test)]
async fn test_version_comparison_update_same_ignored() {
    let version = semantic_version(0, 2020_01_01, 2, 4);
    let env =
        TestEnvBuilder::new().system_version(version.clone()).update_version(version).build().await;

    let result = env
        .connect()
        .check(&mut PackageUrl { url: UPDATE_PACKAGE_URL.to_string() }, None, None)
        .await
        .expect("Send request OK");

    assert_eq!(result, Ok(CheckSuccess::UpdateNotNeeded));
}

#[fasync::run_singlethreaded(test)]
async fn test_version_comparison_opaque_system_version() {
    let env = TestEnvBuilder::new()
        .system_version(SystemVersion::Opaque("2020-08-13T10:27:00+00:00".to_string()))
        .update_version(semantic_version(0, 2020_01_01, 2, 4))
        .build()
        .await;

    let result = env
        .connect()
        .check(&mut PackageUrl { url: UPDATE_PACKAGE_URL.to_string() }, None, None)
        .await
        .expect("Send request OK");

    assert_eq!(result, Err(CheckError::UpdateFailed));
}

#[fasync::run_singlethreaded(test)]
async fn test_version_comparison_opaque_update_version() {
    let env = TestEnvBuilder::new()
        .system_version(semantic_version(0, 2020_01_01, 2, 4))
        .update_version(SystemVersion::Opaque("2020-08-13T10:27:00+00:00".to_string()))
        .build()
        .await;

    let result = env
        .connect()
        .check(&mut PackageUrl { url: UPDATE_PACKAGE_URL.to_string() }, None, None)
        .await
        .expect("Send request OK");

    assert_eq!(result, Err(CheckError::UpdateFailed));
}

#[fasync::run_singlethreaded(test)]
async fn test_version_comparison_no_system_version() {
    let env =
        TestEnvBuilder::new().update_version(semantic_version(0, 2021_01_01, 1, 1)).build().await;

    let result = env
        .connect()
        .check(&mut PackageUrl { url: UPDATE_PACKAGE_URL.to_string() }, None, None)
        .await
        .expect("Send request OK");

    assert_eq!(result, Err(CheckError::UpdateFailed));
}

#[fasync::run_singlethreaded(test)]
async fn test_install_update_fails() {
    let mut env = TestEnvBuilder::new()
        .system_version(semantic_version(0, 1970_01_01, 0, 0))
        .update_version(semantic_version(0, 2020_01_01, 1, 1))
        .state(State::Prepare(PrepareData::empty()))
        .state(State::FailPrepare(FailPrepareData::empty()))
        .build()
        .await;

    let result = env
        .connect()
        .check(
            &mut PackageUrl { url: UPDATE_PACKAGE_URL.to_string() },
            None,
            Some(env.get_monitor_client()),
        )
        .await
        .expect("Send request OK");

    assert_eq!(result, Err(CheckError::UpdateFailed));
    assert!(env.was_update_started().await);
}
