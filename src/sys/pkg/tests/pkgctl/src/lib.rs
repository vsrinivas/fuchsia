// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_pkg::{
        PackageCacheRequest, PackageCacheRequestStream, PackageResolverRequest,
        PackageResolverRequestStream, RepositoryIteratorRequest, RepositoryManagerRequest,
        RepositoryManagerRequestStream,
    },
    fidl_fuchsia_pkg_ext::{
        MirrorConfig, MirrorConfigBuilder, RepositoryConfig, RepositoryConfigBuilder, RepositoryKey,
    },
    fidl_fuchsia_pkg_rewrite::{
        EditTransactionRequest, EngineRequest, EngineRequestStream, RuleIteratorMarker,
        RuleIteratorRequest,
    },
    fidl_fuchsia_pkg_rewrite_ext::Rule,
    fidl_fuchsia_space as fidl_space,
    fidl_fuchsia_sys::{LauncherProxy, TerminationReason},
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{AppBuilder, Output},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_url::pkg_url::{PkgUrl, RepoUrl},
    fuchsia_zircon::Status,
    futures::prelude::*,
    http::Uri,
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
    engine: Arc<MockEngineService>,
    repository_manager: Arc<MockRepositoryManagerService>,
    package_cache: Arc<MockPackageCacheService>,
    package_resolver: Arc<MockPackageResolverService>,
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
            fasync::Task::spawn(
                package_resolver_clone
                    .run_service(stream)
                    .unwrap_or_else(|e| panic!("error running resolver service: {:?}", e)),
            )
            .detach()
        });

        let package_cache = Arc::new(MockPackageCacheService::new());
        let package_cache_clone = package_cache.clone();
        fs.add_fidl_service(move |stream: PackageCacheRequestStream| {
            let package_cache_clone = package_cache_clone.clone();
            fasync::Task::spawn(
                package_cache_clone
                    .run_service(stream)
                    .unwrap_or_else(|e| panic!("error running cache service: {:?}", e)),
            )
            .detach()
        });

        let engine = Arc::new(MockEngineService::new());
        let engine_clone = engine.clone();
        fs.add_fidl_service(move |stream: EngineRequestStream| {
            let engine_clone = engine_clone.clone();
            fasync::Task::spawn(
                engine_clone
                    .run_service(stream)
                    .unwrap_or_else(|e| panic!("error running engine service: {:?}", e)),
            )
            .detach()
        });

        let repository_manager = Arc::new(MockRepositoryManagerService::new());
        let repository_manager_clone = repository_manager.clone();
        fs.add_fidl_service(move |stream: RepositoryManagerRequestStream| {
            let repository_manager_clone = repository_manager_clone.clone();
            fasync::Task::spawn(
                repository_manager_clone
                    .run_service(stream)
                    .unwrap_or_else(|e| panic!("error running repository service: {:?}", e)),
            )
            .detach()
        });

        let space_manager = Arc::new(MockSpaceManagerService::new());
        let space_manager_clone = space_manager.clone();
        fs.add_fidl_service(move |stream: fidl_space::ManagerRequestStream| {
            let space_manager_clone = space_manager_clone.clone();
            fasync::Task::spawn(
                space_manager_clone
                    .run_service(stream)
                    .unwrap_or_else(|e| panic!("error running space service: {:?}", e)),
            )
            .detach()
        });

        let _test_dir = TempDir::new().expect("create test tempdir");

        let repo_config_arg_path = _test_dir.path().join("repo_config");
        create_dir(&repo_config_arg_path).expect("create repo_config_arg dir");

        let env = fs
            .create_salted_nested_environment("pkgctl_env")
            .expect("nested environment to create successfully");
        fasync::Task::spawn(fs.collect()).detach();

        Self {
            env,
            engine,
            repository_manager,
            package_cache,
            package_resolver,
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
        assert_eq!(self.package_cache.captured_args.lock().len(), 0);
        assert_eq!(self.package_resolver.captured_args.lock().len(), 0);
        assert_eq!(self.engine.captured_args.lock().len(), 0);
        assert_eq!(*self.repository_manager.captured_args.lock(), expected_args);
        assert_eq!(*self.space_manager.call_count.lock(), 0);
    }

    fn add_rule(&self, rule: Rule) {
        self.engine.rules.lock().push(rule);
    }

    fn assert_only_engine_called_with(&self, expected_args: Vec<CapturedEngineRequest>) {
        assert_eq!(self.package_cache.captured_args.lock().len(), 0);
        assert_eq!(self.package_resolver.captured_args.lock().len(), 0);
        assert_eq!(*self.engine.captured_args.lock(), expected_args);
        assert_eq!(self.repository_manager.captured_args.lock().len(), 0);
        assert_eq!(*self.space_manager.call_count.lock(), 0);
    }

    fn assert_only_space_manager_called(&self) {
        assert_eq!(self.package_cache.captured_args.lock().len(), 0);
        assert_eq!(self.package_resolver.captured_args.lock().len(), 0);
        assert_eq!(self.engine.captured_args.lock().len(), 0);
        assert_eq!(self.repository_manager.captured_args.lock().len(), 0);
        assert_eq!(*self.space_manager.call_count.lock(), 1);
    }

    fn assert_only_package_resolver_called_with(&self, reqs: Vec<CapturedPackageResolverRequest>) {
        assert_eq!(self.package_cache.captured_args.lock().len(), 0);
        assert_eq!(self.engine.captured_args.lock().len(), 0);
        assert_eq!(self.repository_manager.captured_args.lock().len(), 0);
        assert_eq!(*self.space_manager.call_count.lock(), 0);
        assert_eq!(*self.package_resolver.captured_args.lock(), reqs);
    }

    fn assert_only_package_resolver_and_package_cache_called_with(
        &self,
        resolver_reqs: Vec<CapturedPackageResolverRequest>,
        cache_reqs: Vec<CapturedPackageCacheRequest>,
    ) {
        assert_eq!(*self.package_cache.captured_args.lock(), cache_reqs);
        assert_eq!(self.engine.captured_args.lock().len(), 0);
        assert_eq!(self.repository_manager.captured_args.lock().len(), 0);
        assert_eq!(*self.space_manager.call_count.lock(), 0);
        assert_eq!(*self.package_resolver.captured_args.lock(), resolver_reqs);
    }
}

#[derive(PartialEq, Eq, Debug)]
enum CapturedEngineRequest {
    StartEditTransaction,
}

struct MockEngineService {
    captured_args: Mutex<Vec<CapturedEngineRequest>>,
    rules: Mutex<Vec<Rule>>,
}

impl MockEngineService {
    fn new() -> Self {
        Self { captured_args: Mutex::new(vec![]), rules: Mutex::new(vec![]) }
    }
    async fn run_service(self: Arc<Self>, mut stream: EngineRequestStream) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                EngineRequest::StartEditTransaction {
                    transaction,
                    control_handle: _control_handle,
                } => {
                    self.captured_args.lock().push(CapturedEngineRequest::StartEditTransaction);
                    let mut stream = transaction.into_stream().expect("error here");
                    while let Some(sub_req) = stream.try_next().await? {
                        match sub_req {
                            EditTransactionRequest::ListDynamic {
                                iterator,
                                control_handle: _control_handle,
                            } => {
                                self.list_dynamic_handler(iterator).await?;
                            }
                            EditTransactionRequest::Commit { responder } => {
                                responder.send(&mut Ok(())).expect("send ok");
                            }
                            _ => {
                                panic!("unhandled request method {:?}", sub_req);
                            }
                        }
                    }
                }
                _ => {
                    panic!("unhandled request method {:?}", req);
                }
            }
        }
        Ok(())
    }

    async fn list_dynamic_handler(
        &self,
        iterator: ServerEnd<RuleIteratorMarker>,
    ) -> Result<(), Error> {
        let mut stream = iterator.into_stream().expect("list iterator into_stream");
        let mut rules =
            self.rules.lock().clone().into_iter().map(|rule| rule.into()).collect::<Vec<_>>();
        let mut iter = rules.iter_mut();
        while let Some(RuleIteratorRequest::Next { responder }) = stream.try_next().await? {
            responder.send(&mut iter.by_ref().take(5)).expect("next send")
        }
        Ok(())
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
                    responder.send(&mut Ok(())).expect("send ok");
                }
                RepositoryManagerRequest::Remove { repo_url, responder } => {
                    self.captured_args
                        .lock()
                        .push(CapturedRepositoryManagerRequest::Remove { repo_url });
                    responder.send(&mut Ok(())).expect("send ok");
                }
                RepositoryManagerRequest::AddMirror { repo_url, mirror, responder } => {
                    self.captured_args.lock().push(CapturedRepositoryManagerRequest::AddMirror {
                        repo_url,
                        mirror: MirrorConfig::try_from(mirror).expect("valid mirror config"),
                    });
                    responder.send(&mut Ok(())).expect("send ok");
                }
                RepositoryManagerRequest::RemoveMirror { repo_url, mirror_url, responder } => {
                    self.captured_args.lock().push(
                        CapturedRepositoryManagerRequest::RemoveMirror { repo_url, mirror_url },
                    );
                    responder.send(&mut Ok(())).expect("send ok");
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

#[derive(PartialEq, Debug)]
enum CapturedPackageResolverRequest {
    Resolve,
    GetHash { package_url: String },
}

struct MockPackageResolverService {
    captured_args: Mutex<Vec<CapturedPackageResolverRequest>>,
    get_hash_response: Mutex<Option<Result<fidl_fuchsia_pkg::BlobId, Status>>>,
}

impl MockPackageResolverService {
    fn new() -> Self {
        Self { captured_args: Mutex::new(vec![]), get_hash_response: Mutex::new(None) }
    }
    async fn run_service(
        self: Arc<Self>,
        mut stream: PackageResolverRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                PackageResolverRequest::Resolve { .. } => {
                    self.captured_args.lock().push(CapturedPackageResolverRequest::Resolve);
                }
                PackageResolverRequest::GetHash { package_url, responder } => {
                    self.captured_args.lock().push(CapturedPackageResolverRequest::GetHash {
                        package_url: package_url.url,
                    });
                    responder
                        .send(&mut self.get_hash_response.lock().unwrap().map_err(|s| s.into_raw()))
                        .unwrap()
                }
            }
        }
        Ok(())
    }
}

#[derive(PartialEq, Debug, Eq)]
enum CapturedPackageCacheRequest {
    Open { meta_far_blob_id: fidl_fuchsia_pkg::BlobId },
}

struct MockPackageCacheService {
    captured_args: Mutex<Vec<CapturedPackageCacheRequest>>,
    open_response: Mutex<Option<Result<(), Status>>>,
}

impl MockPackageCacheService {
    fn new() -> Self {
        Self { captured_args: Mutex::new(vec![]), open_response: Mutex::new(Some(Ok(()))) }
    }
    async fn run_service(
        self: Arc<Self>,
        mut stream: PackageCacheRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                PackageCacheRequest::Open { meta_far_blob_id, responder, .. } => {
                    self.captured_args
                        .lock()
                        .push(CapturedPackageCacheRequest::Open { meta_far_blob_id });
                    responder
                        .send(&mut self.open_response.lock().unwrap().map_err(|s| s.into_raw()))
                        .expect("send ok");
                }
                PackageCacheRequest::Get { .. } => panic!("should only support Open requests"),
                PackageCacheRequest::BasePackageIndex { .. } => {
                    panic!("should only support Open requests")
                }
                PackageCacheRequest::Sync { .. } => panic!("should only support Open requests"),
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
    let stdout = std::str::from_utf8(&output.stdout).expect("stdout valid utf8");
    assert_eq!(std::str::from_utf8(output.stderr.as_slice()).expect("stderr valid utf8"), "");
    assert!(output.exit_status.success(), "status: {:?}\nstdout: {}", output.exit_status, stdout);
}

fn assert_stdout(output: &Output, expected: &str) {
    assert_no_errors(output);
    assert_stdout_disregard_errors(output, expected);
}

fn assert_stdout_disregard_errors(output: &Output, expected: &str) {
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
            MirrorConfigBuilder::new("http://example.org".parse::<Uri>().unwrap()).unwrap().build(),
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

#[fasync::run_singlethreaded(test)]
async fn test_dump_dynamic() {
    let env = TestEnv::new();
    env.add_rule(Rule::new("fuchsia.com", "test", "/", "/").unwrap());
    let output = env.run_pkgctl(vec!["rule", "dump-dynamic"]).await;
    let expected_value = serde_json::json!({
        "version": "1",
        "content": [
            {
            "host_match": "fuchsia.com",
            "host_replacement": "test",
            "path_prefix_match": "/",
            "path_prefix_replacement": "/",
            }
        ]
    });
    assert_no_errors(&output);
    let actual_value: serde_json::value::Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(expected_value, actual_value);
    env.assert_only_engine_called_with(vec![CapturedEngineRequest::StartEditTransaction]);
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

#[fasync::run_singlethreaded(test)]
async fn test_gc_success() {
    let env = TestEnv::new();
    *env.space_manager.gc_err.lock() = None;
    let output = env.run_pkgctl(vec!["gc"]).await;
    assert!(output.exit_status.success());
    env.assert_only_space_manager_called();
}

#[fasync::run_singlethreaded(test)]
async fn test_gc_error() {
    let env = TestEnv::new();
    *env.space_manager.gc_err.lock() = Some(fidl_space::ErrorCode::Internal);
    let output = env.run_pkgctl(vec!["gc"]).await;
    assert!(!output.exit_status.success());
    env.assert_only_space_manager_called();
}

#[fasync::run_singlethreaded(test)]
async fn test_experiment_enable_no_admin_service() {
    let env = TestEnv::new();
    let output = env.run_pkgctl(vec!["experiment", "enable", "lightbulb"]).await;
    assert_eq!(output.exit_status.code(), 1);

    // Call another pkgctl command to confirm the tool still works.
    let output = env.run_pkgctl(vec!["gc"]).await;
    assert!(output.exit_status.success());
}

#[fasync::run_singlethreaded(test)]
async fn test_experiment_disable_no_admin_service() {
    let env = TestEnv::new();
    let output = env.run_pkgctl(vec!["experiment", "enable", "lightbulb"]).await;
    assert_eq!(output.exit_status.code(), 1);
}

#[fasync::run_singlethreaded(test)]
async fn test_get_hash_success() {
    let hash = "0000000000000000000000000000000000000000000000000000000000000000";
    let env = TestEnv::new();
    env.package_resolver
        .get_hash_response
        .lock()
        .replace(Ok(hash.parse::<fidl_fuchsia_pkg_ext::BlobId>().unwrap().into()));

    let output = env.run_pkgctl(vec!["get-hash", "the-url"]).await;

    assert_stdout(&output, hash);
    env.assert_only_package_resolver_called_with(vec![CapturedPackageResolverRequest::GetHash {
        package_url: "the-url".into(),
    }]);
}

#[fasync::run_singlethreaded(test)]
async fn test_get_hash_failure() {
    let env = TestEnv::new();
    env.package_resolver.get_hash_response.lock().replace(Err(Status::UNAVAILABLE));

    let output = env.run_pkgctl(vec!["get-hash", "the-url"]).await;

    assert_stderr(&output, "Error: Failed to get package hash with error: UNAVAILABLE\n");
    env.assert_only_package_resolver_called_with(vec![CapturedPackageResolverRequest::GetHash {
        package_url: "the-url".into(),
    }]);
}

#[fasync::run_singlethreaded(test)]
async fn test_pkg_status_success() {
    let hash: fidl_fuchsia_pkg::BlobId =
        "0000000000000000000000000000000000000000000000000000000000000000"
            .parse::<fidl_fuchsia_pkg_ext::BlobId>()
            .unwrap()
            .into();
    let env = TestEnv::new();
    env.package_resolver.get_hash_response.lock().replace(Ok(hash));

    let output = env.run_pkgctl(vec!["pkg-status", "the-url"]).await;

    assert_stdout(&output,
      "Package in registered TUF repo: yes (merkle=0000000000000000000000000000000000000000000000000000000000000000)\n\
      Package on disk: yes (path=/pkgfs/versions/0000000000000000000000000000000000000000000000000000000000000000)\n");
    env.assert_only_package_resolver_and_package_cache_called_with(
        vec![CapturedPackageResolverRequest::GetHash { package_url: "the-url".into() }],
        vec![CapturedPackageCacheRequest::Open { meta_far_blob_id: hash }],
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_pkg_status_fail_pkg_in_tuf_repo_but_not_on_disk() {
    let hash: fidl_fuchsia_pkg::BlobId =
        "0000000000000000000000000000000000000000000000000000000000000000"
            .parse::<fidl_fuchsia_pkg_ext::BlobId>()
            .unwrap()
            .into();
    let env = TestEnv::new();
    env.package_resolver.get_hash_response.lock().replace(Ok(hash));
    env.package_cache.open_response.lock().replace(Err(Status::NOT_FOUND));

    let output = env.run_pkgctl(vec!["pkg-status", "the-url"]).await;

    assert_stdout_disregard_errors(&output,
      "Package in registered TUF repo: yes (merkle=0000000000000000000000000000000000000000000000000000000000000000)\n\
      Package on disk: no\n");
    assert_eq!(output.exit_status.code(), 2);
    env.assert_only_package_resolver_and_package_cache_called_with(
        vec![CapturedPackageResolverRequest::GetHash { package_url: "the-url".into() }],
        vec![CapturedPackageCacheRequest::Open { meta_far_blob_id: hash }],
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_pkg_status_fail_pkg_not_in_tuf_repo() {
    let env = TestEnv::new();
    env.package_resolver.get_hash_response.lock().replace(Err(Status::NOT_FOUND));

    let output = env.run_pkgctl(vec!["pkg-status", "the-url"]).await;

    assert_stdout_disregard_errors(
        &output,
        "Package in registered TUF repo: no\n\
        Package on disk: unknown (did not check since not in tuf repo)\n",
    );
    assert_eq!(output.exit_status.code(), 3);
    env.assert_only_package_resolver_called_with(vec![CapturedPackageResolverRequest::GetHash {
        package_url: "the-url".into(),
    }]);
}
