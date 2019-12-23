// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    failure::Error,
    fidl_fuchsia_pkg::{
        ExperimentToggle as Experiment, PackageCacheRequestStream, PackageResolverAdminRequest,
        PackageResolverAdminRequestStream, PackageResolverRequestStream, RepositoryIteratorRequest,
        RepositoryManagerRequest, RepositoryManagerRequestStream,
    },
    fidl_fuchsia_pkg_ext::{
        MirrorConfig, MirrorConfigBuilder, RepositoryBlobKey, RepositoryConfig,
        RepositoryConfigBuilder, RepositoryKey,
    },
    fidl_fuchsia_pkg_rewrite::EngineRequestStream,
    fidl_fuchsia_space as fidl_space,
    fidl_fuchsia_sys::{LauncherProxy, TerminationReason},
    fidl_fuchsia_update as fidl_update, fuchsia_async as fasync,
    fuchsia_component::{
        client::{AppBuilder, Output},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_url::pkg_url::{PkgUrl, RepoUrl},
    fuchsia_zircon::Status,
    futures::prelude::*,
    parking_lot::Mutex,
    std::{
        convert::TryFrom,
        fs::{create_dir, File},
        iter::FusedIterator,
        path::PathBuf,
        sync::Arc,
    },
    tempfile::TempDir,
};

struct TestEnv {
    env: NestedEnvironment,
    repository_manager: Arc<MockRepositoryManagerService>,
    package_cache: Arc<MockPackageCacheService>,
    package_resolver: Arc<MockPackageResolverService>,
    package_resolver_admin: Arc<MockPackageResolverAdminService>,
    rewrite_engine: Arc<MockRewriteEngineService>,
    update_manager: Arc<MockUpdateManagerService>,
    space_manager: Arc<MockSpaceManagerService>,
    _test_dir: TempDir,
    repo_config_arg_path: PathBuf,
}

impl TestEnv {
    fn launcher(&self) -> &LauncherProxy {
        self.env.launcher()
    }

    fn new() -> Self {
        let mut fs = ServiceFs::new();

        let package_resolver = Arc::new(MockPackageResolverService::new());
        let package_resolver_clone = package_resolver.clone();
        fs.add_fidl_service(move |stream: PackageResolverRequestStream| {
            let package_resolver_clone = package_resolver_clone.clone();
            fasync::spawn(
                package_resolver_clone
                    .run_service(stream)
                    .unwrap_or_else(|e| panic!("error running resolver service: {:?}", e)),
            )
        });

        let package_resolver_admin = Arc::new(MockPackageResolverAdminService::new());
        let package_resolver_admin_clone = package_resolver_admin.clone();
        fs.add_fidl_service(move |stream: PackageResolverAdminRequestStream| {
            let package_resolver_admin_clone = package_resolver_admin_clone.clone();
            fasync::spawn(
                package_resolver_admin_clone
                    .run_service(stream)
                    .unwrap_or_else(|e| panic!("error running resolver admin service: {:?}", e)),
            )
        });

        let package_cache = Arc::new(MockPackageCacheService::new());
        let package_cache_clone = package_cache.clone();
        fs.add_fidl_service(move |stream: PackageCacheRequestStream| {
            let package_cache_clone = package_cache_clone.clone();
            fasync::spawn(
                package_cache_clone
                    .run_service(stream)
                    .unwrap_or_else(|e| panic!("error running cache service: {:?}", e)),
            )
        });

        let rewrite_engine = Arc::new(MockRewriteEngineService::new());
        let rewrite_engine_clone = rewrite_engine.clone();
        fs.add_fidl_service(move |stream: EngineRequestStream| {
            let rewrite_engine_clone = rewrite_engine_clone.clone();
            fasync::spawn(
                rewrite_engine_clone
                    .run_service(stream)
                    .unwrap_or_else(|e| panic!("error running rewrite service: {:?}", e)),
            )
        });

        let repository_manager = Arc::new(MockRepositoryManagerService::new());
        let repository_manager_clone = repository_manager.clone();
        fs.add_fidl_service(move |stream: RepositoryManagerRequestStream| {
            let repository_manager_clone = repository_manager_clone.clone();
            fasync::spawn(
                repository_manager_clone
                    .run_service(stream)
                    .unwrap_or_else(|e| panic!("error running repository service: {:?}", e)),
            )
        });

        let update_manager = Arc::new(MockUpdateManagerService::new());
        let update_manager_clone = update_manager.clone();
        fs.add_fidl_service(move |stream: fidl_update::ManagerRequestStream| {
            let update_manager_clone = update_manager_clone.clone();
            fasync::spawn(
                update_manager_clone
                    .run_service(stream)
                    .unwrap_or_else(|e| panic!("error running update service: {:?}", e)),
            )
        });

        let space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager_clone = space_manager.clone();
        fs.add_fidl_service(move |stream: fidl_space::ManagerRequestStream| {
            let space_manager_clone = space_manager_clone.clone();
            fasync::spawn(
                space_manager_clone
                    .run_service(stream)
                    .unwrap_or_else(|e| panic!("error running space service: {:?}", e)),
            )
        });

        let _test_dir = TempDir::new().expect("create test tempdir");

        let repo_config_arg_path = _test_dir.path().join("repo_config");
        create_dir(&repo_config_arg_path).expect("create repo_config_arg dir");

        let env = fs
            .create_salted_nested_environment("pkgctl_env")
            .expect("nested environment to create successfully");
        fasync::spawn(fs.collect());

        Self {
            env,
            repository_manager,
            package_cache,
            package_resolver,
            package_resolver_admin,
            rewrite_engine,
            update_manager,
            space_manager,
            _test_dir,
            repo_config_arg_path,
        }
    }

    async fn run_pkgctl<'a>(&'a self, args: Vec<&'a str>) -> Output {
        let launcher = self.launcher();
        let repo_config_arg_dir =
            File::open(&self.repo_config_arg_path).expect("open repo_config_arg dir");

        let pkgctl =
            AppBuilder::new("fuchsia-pkg://fuchsia.com/pkgctl-integration-tests#meta/pkgctl.cmx")
                .args(args)
                .add_dir_to_namespace("/repo-configs".to_string(), repo_config_arg_dir)
                .expect("pkgctl app");

        let output = pkgctl
            .output(launcher)
            .expect("pkgctl to launch")
            .await
            .expect("no errors while waiting for exit");
        assert_eq!(output.exit_status.reason(), TerminationReason::Exited);
        output
    }

    fn add_repository(&self, repo_config: RepositoryConfig) {
        self.repository_manager.repos.lock().push(repo_config);
    }

    fn assert_only_repository_manager_called_with(
        &self,
        expected_args: Vec<CapturedRepositoryManagerRequest>,
    ) {
        assert_eq!(*self.package_cache.call_count.lock(), 0);
        assert_eq!(*self.package_resolver.call_count.lock(), 0);
        assert_eq!(self.package_resolver_admin.take_event(), None);
        assert_eq!(*self.rewrite_engine.call_count.lock(), 0);
        assert_eq!(*self.repository_manager.captured_args.lock(), expected_args);
        assert_eq!(self.update_manager.captured_args.lock().len(), 0);
        assert_eq!(*self.space_manager.call_count.lock(), 0);
    }

    fn assert_only_update_manager_called_with(
        &self,
        expected_args: Vec<CapturedUpdateManagerRequest>,
    ) {
        assert_eq!(*self.package_cache.call_count.lock(), 0);
        assert_eq!(*self.package_resolver.call_count.lock(), 0);
        assert_eq!(self.package_resolver_admin.take_event(), None);
        assert_eq!(*self.rewrite_engine.call_count.lock(), 0);
        assert_eq!(self.repository_manager.captured_args.lock().len(), 0);
        assert_eq!(*self.update_manager.captured_args.lock(), expected_args);
        assert_eq!(*self.space_manager.call_count.lock(), 0);
    }

    fn assert_only_space_manager_called(&self) {
        assert_eq!(*self.package_cache.call_count.lock(), 0);
        assert_eq!(*self.package_resolver.call_count.lock(), 0);
        assert_eq!(self.package_resolver_admin.take_event(), None);
        assert_eq!(*self.rewrite_engine.call_count.lock(), 0);
        assert_eq!(self.repository_manager.captured_args.lock().len(), 0);
        assert_eq!(self.update_manager.captured_args.lock().len(), 0);
        assert_eq!(*self.space_manager.call_count.lock(), 1);
    }

    fn assert_only_package_resolver_admin_called_with(&self, event: ExperimentEvent) {
        assert_eq!(*self.package_cache.call_count.lock(), 0);
        assert_eq!(*self.package_resolver.call_count.lock(), 0);
        assert_eq!(*self.rewrite_engine.call_count.lock(), 0);
        assert_eq!(self.repository_manager.captured_args.lock().len(), 0);
        assert_eq!(self.update_manager.captured_args.lock().len(), 0);
        assert_eq!(*self.space_manager.call_count.lock(), 0);
        assert_eq!(self.package_resolver_admin.take_event(), Some(event));
    }
}

#[derive(PartialEq, Eq, Debug)]
enum CapturedRepositoryManagerRequest {
    Add { repo: RepositoryConfig },
    Remove { repo_url: String },
    AddMirror { repo_url: String, mirror: MirrorConfig },
    RemoveMirror { repo_url: String, mirror_url: String },
    List,
}

struct MockRepositoryManagerService {
    captured_args: Mutex<Vec<CapturedRepositoryManagerRequest>>,
    repos: Mutex<Vec<RepositoryConfig>>,
}

impl MockRepositoryManagerService {
    fn new() -> Self {
        Self { captured_args: Mutex::new(vec![]), repos: Mutex::new(vec![]) }
    }
    async fn run_service(
        self: Arc<Self>,
        mut stream: RepositoryManagerRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                RepositoryManagerRequest::Add { repo, responder } => {
                    self.captured_args.lock().push(CapturedRepositoryManagerRequest::Add {
                        repo: RepositoryConfig::try_from(repo).expect("valid repo config"),
                    });
                    responder.send(Status::OK.into_raw()).expect("send ok");
                }
                RepositoryManagerRequest::Remove { repo_url, responder } => {
                    self.captured_args
                        .lock()
                        .push(CapturedRepositoryManagerRequest::Remove { repo_url });
                    responder.send(Status::OK.into_raw()).expect("send ok");
                }
                RepositoryManagerRequest::AddMirror { repo_url, mirror, responder } => {
                    self.captured_args.lock().push(CapturedRepositoryManagerRequest::AddMirror {
                        repo_url,
                        mirror: MirrorConfig::try_from(mirror).expect("valid mirror config"),
                    });
                    responder.send(Status::OK.into_raw()).expect("send ok");
                }
                RepositoryManagerRequest::RemoveMirror { repo_url, mirror_url, responder } => {
                    self.captured_args.lock().push(
                        CapturedRepositoryManagerRequest::RemoveMirror { repo_url, mirror_url },
                    );
                    responder.send(Status::OK.into_raw()).expect("send ok");
                }
                RepositoryManagerRequest::List { iterator, control_handle: _control_handle } => {
                    self.captured_args.lock().push(CapturedRepositoryManagerRequest::List);
                    let mut stream = iterator.into_stream().expect("list iterator into_stream");
                    let mut repos = self.repos.lock().clone().into_iter().map(|r| r.into());
                    // repos must be fused b/c the Next() fidl method should return an empty vector
                    // forever after iteration is complete
                    let _: &dyn FusedIterator<Item = _> = &repos;
                    while let Some(RepositoryIteratorRequest::Next { responder }) =
                        stream.try_next().await?
                    {
                        responder.send(&mut repos.by_ref().take(5)).expect("next send")
                    }
                }
            }
        }
        Ok(())
    }
}

struct MockPackageResolverService {
    call_count: Mutex<u32>,
}

impl MockPackageResolverService {
    fn new() -> Self {
        Self { call_count: Mutex::new(0) }
    }
    async fn run_service(
        self: Arc<Self>,
        mut stream: PackageResolverRequestStream,
    ) -> Result<(), Error> {
        while let Some(_req) = stream.try_next().await? {
            *self.call_count.lock() += 1;
        }
        Ok(())
    }
}

#[derive(Debug, PartialEq, Eq)]
enum ExperimentEvent {
    Enable(Experiment),
    Disable(Experiment),
}

struct MockPackageResolverAdminService {
    event: Mutex<Option<ExperimentEvent>>,
}

impl MockPackageResolverAdminService {
    fn new() -> Self {
        Self { event: Mutex::new(None) }
    }
    fn take_event(&self) -> Option<ExperimentEvent> {
        self.event.lock().take()
    }
    async fn run_service(
        self: Arc<Self>,
        mut stream: PackageResolverAdminRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                PackageResolverAdminRequest::SetExperimentState {
                    experiment_id,
                    state,
                    responder,
                } => {
                    let event = if state {
                        ExperimentEvent::Enable(experiment_id)
                    } else {
                        ExperimentEvent::Disable(experiment_id)
                    };
                    let prev = self.event.lock().replace(event);
                    assert_eq!(prev, None);
                    responder.send().expect("pkgctl to wait for response");
                }
            }
        }
        Ok(())
    }
}

struct MockPackageCacheService {
    call_count: Mutex<u32>,
}

impl MockPackageCacheService {
    fn new() -> Self {
        Self { call_count: Mutex::new(0) }
    }
    async fn run_service(
        self: Arc<Self>,
        mut stream: PackageCacheRequestStream,
    ) -> Result<(), Error> {
        while let Some(_req) = stream.try_next().await? {
            *self.call_count.lock() += 1;
        }
        Ok(())
    }
}

struct MockRewriteEngineService {
    call_count: Mutex<u32>,
}

impl MockRewriteEngineService {
    fn new() -> Self {
        Self { call_count: Mutex::new(0) }
    }
    async fn run_service(self: Arc<Self>, mut stream: EngineRequestStream) -> Result<(), Error> {
        while let Some(_req) = stream.try_next().await? {
            *self.call_count.lock() += 1;
        }
        Ok(())
    }
}

#[derive(PartialEq, Debug)]
enum CapturedUpdateManagerRequest {
    CheckNow { options: fidl_update::Options },
    GetState,
    AddMonitor,
}

// fidl_update::Options is not Eq, but it contains only an Option of an Eq
impl Eq for CapturedUpdateManagerRequest {}

struct MockUpdateManagerService {
    captured_args: Mutex<Vec<CapturedUpdateManagerRequest>>,
    check_now_response: Mutex<fidl_update::CheckStartedResult>,
}

impl MockUpdateManagerService {
    fn new() -> Self {
        Self {
            captured_args: Mutex::new(vec![]),
            check_now_response: Mutex::new(fidl_update::CheckStartedResult::Started),
        }
    }
    async fn run_service(
        self: Arc<Self>,
        mut stream: fidl_update::ManagerRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                fidl_update::ManagerRequest::CheckNow { options, monitor: _monitor, responder } => {
                    self.captured_args
                        .lock()
                        .push(CapturedUpdateManagerRequest::CheckNow { options });
                    responder.send(*self.check_now_response.lock())?;
                }
                fidl_update::ManagerRequest::GetState { responder: _responder } => {
                    self.captured_args.lock().push(CapturedUpdateManagerRequest::GetState);
                }
                fidl_update::ManagerRequest::AddMonitor {
                    monitor: _monitor,
                    control_handle: _control_handler,
                } => {
                    self.captured_args.lock().push(CapturedUpdateManagerRequest::AddMonitor);
                }
            }
        }
        Ok(())
    }
}

struct MockSpaceManagerService {
    call_count: Mutex<u32>,
    gc_err: Mutex<Option<fidl_space::ErrorCode>>,
}

impl MockSpaceManagerService {
    fn new() -> Self {
        Self { call_count: Mutex::new(0), gc_err: Mutex::new(None) }
    }
    async fn run_service(
        self: Arc<Self>,
        mut stream: fidl_space::ManagerRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            *self.call_count.lock() += 1;

            match req {
                fidl_space::ManagerRequest::Gc { responder } => {
                    if let Some(e) = *self.gc_err.lock() {
                        responder.send(&mut Err(e))?;
                    } else {
                        responder.send(&mut Ok(()))?;
                    }
                }
            }
        }
        Ok(())
    }
}

fn assert_no_errors(output: &Output) {
    assert_eq!(std::str::from_utf8(output.stderr.as_slice()).expect("stdout valid utf8"), "");
    assert!(output.exit_status.success());
}

fn assert_stdout(output: &Output, expected: &str) {
    assert_no_errors(output);
    assert_eq!(std::str::from_utf8(output.stdout.as_slice()).expect("stdout utf8"), expected);
}

fn assert_stderr(output: &Output, expected: &str) {
    assert!(!output.exit_status.success());
    assert_eq!(std::str::from_utf8(output.stderr.as_slice()).expect("stderr valid utf8"), expected);
}

fn make_test_repo_config() -> RepositoryConfig {
    RepositoryConfigBuilder::new(RepoUrl::new("example.com".to_string()).expect("valid url"))
        .add_root_key(RepositoryKey::Ed25519(vec![0u8]))
        .add_mirror(
            MirrorConfigBuilder::new("example.org")
                .blob_key(RepositoryBlobKey::Aes(vec![1u8]))
                .build(),
        )
        .update_package_url(
            PkgUrl::parse("fuchsia-pkg://update.example.com/update").expect("valid PkgUrl"),
        )
        .build()
}

#[fasync::run_singlethreaded(test)]
async fn test_repo() {
    let env = TestEnv::new();
    env.add_repository(
        RepositoryConfigBuilder::new(RepoUrl::new("example.com".to_string()).expect("valid url"))
            .build(),
    );

    let output = env.run_pkgctl(vec!["repo"]).await;

    assert_stdout(&output, "fuchsia-pkg://example.com\n");
    env.assert_only_repository_manager_called_with(vec![CapturedRepositoryManagerRequest::List]);
}

#[fasync::run_singlethreaded(test)]
async fn test_repo_sorts_lines() {
    let env = TestEnv::new();
    env.add_repository(
        RepositoryConfigBuilder::new(RepoUrl::new("z.com".to_string()).expect("valid url")).build(),
    );
    env.add_repository(
        RepositoryConfigBuilder::new(RepoUrl::new("a.com".to_string()).expect("valid url")).build(),
    );

    let output = env.run_pkgctl(vec!["repo"]).await;

    assert_stdout(&output, "fuchsia-pkg://a.com\nfuchsia-pkg://z.com\n");
}

macro_rules! repo_verbose_tests {
    ($($test_name:ident: $flag:expr,)*) => {
        $(
            #[fasync::run_singlethreaded(test)]
            async fn $test_name() {
                let env = TestEnv::new();
                let repo_config = make_test_repo_config();
                env.add_repository(repo_config.clone());

                let output = env.run_pkgctl(vec!["repo", $flag]).await;

                assert_no_errors(&output);
                let round_trip_repo_configs: Vec<RepositoryConfig> =
                    serde_json::from_slice(output.stdout.as_slice()).expect("valid json");
                assert_eq!(round_trip_repo_configs, vec![repo_config]);
                env.assert_only_repository_manager_called_with(vec![CapturedRepositoryManagerRequest::List]);
            }
        )*
    }
}

repo_verbose_tests! {
    test_repo_verbose_short: "-v",
    test_repo_verbose_long: "--verbose",
}

#[fasync::run_singlethreaded(test)]
async fn test_repo_rm() {
    let env = TestEnv::new();

    let output = env.run_pkgctl(vec!["repo", "rm", "the-url"]).await;

    assert_stdout(&output, "");
    env.assert_only_repository_manager_called_with(vec![
        CapturedRepositoryManagerRequest::Remove { repo_url: "the-url".to_string() },
    ]);
}

macro_rules! repo_add_tests {
    ($($test_name:ident: $flag:expr,)*) => {
        $(
            #[fasync::run_singlethreaded(test)]
            async fn $test_name() {
                let env = TestEnv::new();
                let repo_config = make_test_repo_config();
                let f =
                    File::create(env.repo_config_arg_path.join("the-config")).expect("create repo config file");
                serde_json::to_writer(f, &repo_config).expect("write RepositoryConfig json");

                let output = env.run_pkgctl(vec!["repo", "add", $flag, "/repo-configs/the-config"]).await;

                assert_stdout(&output, "");
                env.assert_only_repository_manager_called_with(vec![CapturedRepositoryManagerRequest::Add {
                    repo: repo_config,
                }]);
            }
        )*
    }
}

repo_add_tests! {
    test_repo_add_short: "-f",
    test_repo_add_long: "--file",
}

async fn test_update_success_impl(check_now_response: fidl_update::CheckStartedResult) {
    let env = TestEnv::new();
    *env.update_manager.check_now_response.lock() = check_now_response;

    let output = env.run_pkgctl(vec!["update"]).await;

    assert_stdout(&output, "");
    env.assert_only_update_manager_called_with(vec![CapturedUpdateManagerRequest::CheckNow {
        options: fidl_update::Options { initiator: Some(fidl_update::Initiator::User) },
    }]);
}

#[fasync::run_singlethreaded(test)]
async fn test_update_success_started() {
    test_update_success_impl(fidl_update::CheckStartedResult::Started).await;
}

#[fasync::run_singlethreaded(test)]
async fn test_update_success_in_progress() {
    test_update_success_impl(fidl_update::CheckStartedResult::InProgress).await;
}

#[fasync::run_singlethreaded(test)]
async fn test_update_throttled_is_error() {
    let env = TestEnv::new();
    *env.update_manager.check_now_response.lock() = fidl_update::CheckStartedResult::Throttled;

    let output = env.run_pkgctl(vec!["update"]).await;

    assert_stderr(&output, "Error: ErrorMessage { msg: \"Update check was throttled.\" }\n");
    env.assert_only_update_manager_called_with(vec![CapturedUpdateManagerRequest::CheckNow {
        options: fidl_update::Options { initiator: Some(fidl_update::Initiator::User) },
    }]);
}

#[fasync::run_singlethreaded(test)]
async fn test_gc_success() {
    let env = TestEnv::new();
    *env.space_manager.gc_err.lock() = None;
    let output = env.run_pkgctl(vec!["gc"]).await;
    assert!(output.exit_status.success());
    env.assert_only_space_manager_called();
}

#[fasync::run_singlethreaded(test)]
async fn test_gc_fail() {
    let env = TestEnv::new();
    *env.space_manager.gc_err.lock() = Some(fidl_space::ErrorCode::Internal);
    let output = env.run_pkgctl(vec!["gc"]).await;
    assert!(!output.exit_status.success());
    env.assert_only_space_manager_called();
}

#[fasync::run_singlethreaded(test)]
async fn test_experiment_enable() {
    let env = TestEnv::new();
    env.run_pkgctl(vec!["experiment", "enable", "lightbulb"]).await.ok().unwrap();
    env.assert_only_package_resolver_admin_called_with(ExperimentEvent::Enable(
        Experiment::Lightbulb,
    ));
}

#[fasync::run_singlethreaded(test)]
async fn test_experiment_disable() {
    let env = TestEnv::new();
    env.run_pkgctl(vec!["experiment", "disable", "lightbulb"]).await.ok().unwrap();
    env.assert_only_package_resolver_admin_called_with(ExperimentEvent::Disable(
        Experiment::Lightbulb,
    ));
}
