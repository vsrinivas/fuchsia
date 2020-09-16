// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_pkg::{PackageResolverMarker, PackageUrl},
    fidl_fuchsia_update_installer::{InstallerRequestStream, State, *},
    fidl_fuchsia_update_usb::{CheckSuccess, CheckerMarker, CheckerProxy},
    fuchsia_async::{self as fasync, Task},
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_pkg_testing::{PackageBuilder, RepositoryBuilder},
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
    system_version: SystemVersion,
    states: Vec<State>,
}

impl TestEnvBuilder {
    pub fn new() -> Self {
        TestEnvBuilder {
            update_version: SystemVersion::Opaque("".to_string()),
            system_version: SystemVersion::Opaque("".to_string()),
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
        self.system_version = version;
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
                    url: _,
                    options: _,
                    monitor,
                    reboot_controller,
                    responder,
                } => {
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
    _installer: Arc<MockInstaller>,
}

impl TestEnv {
    pub async fn new(
        resolver: ResolverForTest,
        system_version: SystemVersion,
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

        std::fs::write(
            format!("{}/version", build_info_dir.path().to_string_lossy()),
            system_version.to_string(),
        )
        .expect("Writing system version succeeds");

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

        TestEnv {
            _resolver: resolver,
            checker,
            _env: env,
            _svcfs_task: svcfs_task,
            _installer: installer,
        }
    }

    pub fn connect(&self) -> CheckerProxy {
        self.checker.connect_to_service::<CheckerMarker>().expect("Connect to checker succeeds")
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_install_update_succeeds() {
    let env = TestEnvBuilder::new()
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
        .check(&mut PackageUrl { url: UPDATE_PACKAGE_URL.to_string() }, None, None)
        .await
        .expect("Send request OK");

    assert_eq!(result, Ok(CheckSuccess::UpdatePerformed));
}
