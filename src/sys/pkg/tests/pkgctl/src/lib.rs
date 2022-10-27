// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]
#![cfg(test)]
use {
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_pkg::{
        self as fpkg, PackageCacheRequest, PackageCacheRequestStream, PackageResolverRequest,
        PackageResolverRequestStream, RepositoryIteratorRequest, RepositoryManagerRequest,
        RepositoryManagerRequestStream, ResolveError,
    },
    fidl_fuchsia_pkg_ext::{
        MirrorConfig, MirrorConfigBuilder, RepositoryConfig, RepositoryConfigBuilder,
        RepositoryKey, RepositoryStorageType,
    },
    fidl_fuchsia_pkg_rewrite::{
        EditTransactionRequest, EngineRequest, EngineRequestStream, RuleIteratorMarker,
        RuleIteratorRequest,
    },
    fidl_fuchsia_pkg_rewrite_ext::Rule,
    fidl_fuchsia_space as fidl_space, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_hyper_test_support::{handler::StaticResponse, TestServer},
    fuchsia_url::RepositoryUrl,
    fuchsia_zircon::Status,
    futures::prelude::*,
    http::Uri,
    parking_lot::Mutex,
    shell_process::ProcessOutput,
    std::{
        convert::TryFrom,
        fs::{create_dir, File},
        io::Write,
        iter::FusedIterator,
        path::PathBuf,
        sync::Arc,
    },
    tempfile::TempDir,
    vfs::directory::entry::DirectoryEntry,
};

const BINARY_PATH: &str = "/pkg/bin/pkgctl";

struct TestEnv {
    engine: Arc<MockEngineService>,
    repository_manager: Arc<MockRepositoryManagerService>,
    package_cache: Arc<MockPackageCacheService>,
    package_resolver: Arc<MockPackageResolverService>,
    space_manager: Arc<MockSpaceManagerService>,
    _test_dir: TempDir,
    repo_config_arg_path: PathBuf,
    svc_proxy: fidl_fuchsia_io::DirectoryProxy,
}

impl TestEnv {
    fn new() -> Self {
        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_net_http::LoaderMarker, _>();

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

        let (svc_proxy, svc_server_end) = fidl::endpoints::create_proxy().expect("create channel");

        let _env = fs.serve_connection(svc_server_end).expect("serve connection");

        fasync::Task::spawn(fs.collect()).detach();

        Self {
            engine,
            repository_manager,
            package_cache,
            package_resolver,
            space_manager,
            _test_dir,
            repo_config_arg_path,
            svc_proxy,
        }
    }

    async fn run_pkgctl<'a>(&'a self, args: Vec<&'a str>) -> ProcessOutput {
        let repo_config_arg_dir = fuchsia_fs::directory::open_in_namespace(
            &self.repo_config_arg_path.display().to_string(),
            fio::OpenFlags::RIGHT_READABLE,
        )
        .unwrap();

        shell_process::run_process(
            BINARY_PATH,
            args,
            [("/svc", &self.svc_proxy), ("/repo-configs", &repo_config_arg_dir)],
        )
        .await
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
    Resolve { package_url: String },
    GetHash { package_url: String },
}

struct MockPackageResolverService {
    captured_args: Mutex<Vec<CapturedPackageResolverRequest>>,
    get_hash_response: Mutex<Option<Result<fidl_fuchsia_pkg::BlobId, Status>>>,
    resolve_response: Mutex<
        Option<(
            Arc<dyn DirectoryEntry>,
            Result<fpkg::ResolutionContext, fidl_fuchsia_pkg::ResolveError>,
        )>,
    >,
}

impl MockPackageResolverService {
    fn new() -> Self {
        Self {
            captured_args: Mutex::new(vec![]),
            get_hash_response: Mutex::new(None),
            resolve_response: Mutex::new(None),
        }
    }
    async fn run_service(
        self: Arc<Self>,
        mut stream: PackageResolverRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                PackageResolverRequest::Resolve { package_url, dir, responder } => {
                    self.captured_args
                        .lock()
                        .push(CapturedPackageResolverRequest::Resolve { package_url });

                    let (dir_server, mut res) = self.resolve_response.lock().take().unwrap();
                    let () = dir_server.open(
                        vfs::execution_scope::ExecutionScope::new(),
                        fio::OpenFlags::RIGHT_READABLE,
                        0,
                        vfs::path::Path::dot(),
                        dir.into_channel().into(),
                    );
                    responder.send(&mut res).unwrap()
                }
                PackageResolverRequest::ResolveWithContext {
                    package_url: _,
                    context: _,
                    dir: _,
                    responder,
                } => {
                    // not implemented
                    responder.send(&mut Err(ResolveError::Internal)).unwrap()
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
                PackageCacheRequest::Get { .. } => {
                    panic!("should only support Open requests, received Get")
                }
                PackageCacheRequest::BasePackageIndex { .. } => {
                    panic!("should only support Open requests, received BasePackageIndex")
                }
                PackageCacheRequest::CachePackageIndex { .. } => {
                    panic!("should only support Open requests, received CachePackageIndex")
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

fn assert_no_errors(output: &ProcessOutput) {
    assert!(output.is_ok(), "status: {:?}\nstdout: {}", output.return_code(), output.stdout_str());
}

fn assert_stdout(output: &ProcessOutput, expected: &str) {
    assert_no_errors(output);
    assert_stdout_disregard_errors(output, expected);
}

fn assert_stdout_disregard_errors(output: &ProcessOutput, expected: &str) {
    assert_eq!(output.stdout_str(), expected);
}

fn assert_stderr(output: &ProcessOutput, expected: &str) {
    assert!(!output.is_ok());
    assert_eq!(output.stderr_str(), expected);
}

fn make_test_repo_config() -> RepositoryConfig {
    RepositoryConfigBuilder::new(
        RepositoryUrl::parse_host("example.com".to_string()).expect("valid url"),
    )
    .add_root_key(RepositoryKey::Ed25519(vec![0u8]))
    .add_mirror(
        MirrorConfigBuilder::new("http://example.org".parse::<Uri>().unwrap()).unwrap().build(),
    )
    .build()
}

// This builds a v2 RepositoryConfig that is expected to be in the repository manager add request
// when pkgctl is provided with the V1_LEGACY_TEST_REPO_JSON structure below as input.
fn make_v1_legacy_expected_test_repo_config(persist: bool) -> RepositoryConfig {
    let storage_type = match persist {
        true => RepositoryStorageType::Persistent,
        false => RepositoryStorageType::Ephemeral,
    };
    RepositoryConfigBuilder::new(
        RepositoryUrl::parse_host("legacy-repo".to_string()).expect("valid url"),
    )
    .add_root_key(RepositoryKey::Ed25519(vec![0u8]))
    .add_mirror(
        MirrorConfigBuilder::new("http://legacy.org".parse::<Uri>().unwrap())
            .unwrap()
            .subscribe(true)
            .build(),
    )
    .repo_storage_type(storage_type)
    .build()
}

const V1_LEGACY_TEST_REPO_JSON: &str = r#"{
    "ID":"legacy_repo",
    "RepoURL":"http://legacy.org",
    "BlobRepoURL":"http://legacy.org/blobs",
    "RatePeriod":60,
    "RootKeys":[
        {
            "type":"ed25519",
            "value":"00"
        }
    ],
    "rootVersion":1,
    "rootThreshold":1,
    "StatusConfig":{
        "Enabled":true
    },
    "Auto":true,
    "BlobKey":null
}"#;

#[fasync::run_singlethreaded(test)]
async fn test_repo() {
    let env = TestEnv::new();
    env.add_repository(
        RepositoryConfigBuilder::new(
            RepositoryUrl::parse_host("example.com".to_string()).expect("valid url"),
        )
        .build(),
    );

    let output = env.run_pkgctl(vec!["repo"]).await;

    assert_stdout(&output, "fuchsia-pkg://example.com\n");
    env.assert_only_repository_manager_called_with(vec![CapturedRepositoryManagerRequest::List]);
}

#[fasync::run_singlethreaded(test)]
async fn test_repo_show() {
    let env = TestEnv::new();

    // Add two repos, then verify we can get the details from the one we request.
    env.add_repository(
        RepositoryConfigBuilder::new(
            RepositoryUrl::parse_host("z.com".to_string()).expect("valid url"),
        )
        .build(),
    );
    let repo = RepositoryConfigBuilder::new(
        RepositoryUrl::parse_host("a.com".to_string()).expect("valid url"),
    )
    .build();
    env.add_repository(repo.clone());

    // The JSON conversion does not contain the trailing newline that we get when actually running
    // the command on the command line.
    let expected = serde_json::to_string_pretty(&repo).expect("valid json") + "\n";
    let output = env.run_pkgctl(vec!["repo", "show", "fuchsia-pkg://a.com"]).await;

    assert_stdout(&output, &expected);
    env.assert_only_repository_manager_called_with(vec![CapturedRepositoryManagerRequest::List]);
}

#[fasync::run_singlethreaded(test)]
async fn test_repo_sorts_lines() {
    let env = TestEnv::new();
    env.add_repository(
        RepositoryConfigBuilder::new(
            RepositoryUrl::parse_host("z.com".to_string()).expect("valid url"),
        )
        .build(),
    );
    env.add_repository(
        RepositoryConfigBuilder::new(
            RepositoryUrl::parse_host("a.com".to_string()).expect("valid url"),
        )
        .build(),
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
    ($($test_name:ident: $source:expr, $version:expr, $name:expr,)*) => {
        $(
            #[fasync::run_singlethreaded(test)]
            async fn $test_name() {
                let env = TestEnv::new();

                let repo_config = match $version {
                    "1" =>  make_v1_legacy_expected_test_repo_config(false),
                    _ => make_test_repo_config(),
                };

                let output = match $source {
                    "file" => {
                        let mut f =
                            File::create(env.repo_config_arg_path.join("the-config")).expect("create repo config file");
                        match $version {
                            "1" => { f.write_all(V1_LEGACY_TEST_REPO_JSON.as_bytes()).expect("write v1 SourceConfig json"); },
                            _ => { serde_json::to_writer(f, &repo_config).expect("write RepositoryConfig json"); }
                        };
                        let args = if $name == "" {
                            vec!["repo", "add", $source, "-f", $version, "/repo-configs/the-config"]
                        } else {
                            vec!["repo", "add", $source, "-f", $version, "-n", $name, "/repo-configs/the-config"]
                        };
                        env.run_pkgctl(args).await
                    },
                    "url" => {
                        let response = match $version {
                            "1" => StaticResponse::ok_body(V1_LEGACY_TEST_REPO_JSON),
                            _ => StaticResponse::ok_body(serde_json::to_string(&repo_config).unwrap()),
                        };
                        let server = TestServer::builder().handler(response).start();
                        let local_url = server.local_url_for_path("some/path").to_owned();
                        let args = if $name == "" {
                            vec!["repo", "add", $source, "-f", $version, &local_url]
                        } else {
                            vec!["repo", "add", $source, "-f", $version, "-n", $name, &local_url]
                        };
                        env.run_pkgctl(args).await
                    },
                    // Unsupported source
                    _ => env.run_pkgctl(vec!["repo", "add", $source, "-f", $version]).await,
                };

                assert_stdout(&output, "");
                env.assert_only_repository_manager_called_with(vec![CapturedRepositoryManagerRequest::Add {
                    repo: repo_config,
                }]);
            }
        )*
    }
}

repo_add_tests! {
    test_repo_add_v1_file: "file", "1", "",
    test_repo_add_v1_file_with_name: "file", "1", "legacy-repo",
    test_repo_add_v2_file: "file", "2", "",
    test_repo_add_v2_file_with_name: "file", "2", "example.com",
    test_repo_add_v1_url: "url", "1", "",
    test_repo_add_v1_url_with_name: "url", "1", "legacy-repo",
    test_repo_add_v2_url: "url", "2", "",
    test_repo_add_v2_url_with_name: "url", "2", "example.com",
}

#[fasync::run_singlethreaded(test)]
async fn test_repo_add_persistent_v1_url() {
    let env = TestEnv::new();

    let repo_config = make_v1_legacy_expected_test_repo_config(true);
    let response = StaticResponse::ok_body(V1_LEGACY_TEST_REPO_JSON);
    let server = TestServer::builder().handler(response).start();
    let local_url = server.local_url_for_path("some/path").to_owned();
    let args = vec!["repo", "add", "url", "-p", "-f", "1", &local_url];
    let output = env.run_pkgctl(args).await;

    assert_stdout(&output, "");
    env.assert_only_repository_manager_called_with(vec![CapturedRepositoryManagerRequest::Add {
        repo: repo_config,
    }]);
}

#[fasync::run_singlethreaded(test)]
async fn test_gc_success() {
    let env = TestEnv::new();
    *env.space_manager.gc_err.lock() = None;
    let output = env.run_pkgctl(vec!["gc"]).await;
    assert!(output.is_ok());
    env.assert_only_space_manager_called();
}

#[fasync::run_singlethreaded(test)]
async fn test_gc_error() {
    let env = TestEnv::new();
    *env.space_manager.gc_err.lock() = Some(fidl_space::ErrorCode::Internal);
    let output = env.run_pkgctl(vec!["gc"]).await;
    assert!(!output.is_ok());
    env.assert_only_space_manager_called();
}

#[fasync::run_singlethreaded(test)]
async fn test_experiment_enable_no_admin_service() {
    let env = TestEnv::new();
    let output = env.run_pkgctl(vec!["experiment", "enable", "lightbulb"]).await;
    assert_eq!(output.return_code(), 1);

    // Call another pkgctl command to confirm the tool still works.
    let output = env.run_pkgctl(vec!["gc"]).await;
    assert!(output.is_ok());
}

#[fasync::run_singlethreaded(test)]
async fn test_experiment_disable_no_admin_service() {
    let env = TestEnv::new();
    let output = env.run_pkgctl(vec!["experiment", "enable", "lightbulb"]).await;
    assert_eq!(output.return_code(), 1);
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

    assert_stdout(&output, &(hash.to_owned() + "\n"));
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
    assert_eq!(output.return_code(), 2);
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
    assert_eq!(output.return_code(), 3);
    env.assert_only_package_resolver_called_with(vec![CapturedPackageResolverRequest::GetHash {
        package_url: "the-url".into(),
    }]);
}

#[fasync::run_singlethreaded(test)]
async fn test_resolve() {
    let env = TestEnv::new();
    env.package_resolver.resolve_response.lock().replace((
        vfs::pseudo_directory! { "meta" => vfs::pseudo_directory! {} },
        Ok(fpkg::ResolutionContext { bytes: vec![] }),
    ));

    let output = env.run_pkgctl(vec!["resolve", "the-url"]).await;

    assert_stdout(&output, "resolving the-url\n");

    env.assert_only_package_resolver_called_with(vec![CapturedPackageResolverRequest::Resolve {
        package_url: "the-url".into(),
    }]);
}

#[fasync::run_singlethreaded(test)]
async fn test_resolve_verbose() {
    let env = TestEnv::new();
    env.package_resolver.resolve_response.lock().replace((
        vfs::pseudo_directory! { "meta" => vfs::pseudo_directory! {} },
        Ok(fpkg::ResolutionContext { bytes: vec![] }),
    ));

    let output = env.run_pkgctl(vec!["resolve", "the-url", "--verbose"]).await;

    assert_stdout(&output, "resolving the-url\npackage contents:\n/meta\n");

    env.assert_only_package_resolver_called_with(vec![CapturedPackageResolverRequest::Resolve {
        package_url: "the-url".into(),
    }]);
}
