// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::{ClientEnd, DiscoverableService},
    fidl_fuchsia_boot::{ArgumentsRequest, ArgumentsRequestStream},
    fidl_fuchsia_inspect::TreeMarker,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, CLONE_FLAG_SAME_RIGHTS},
    fidl_fuchsia_pkg::{
        ExperimentToggle as Experiment, PackageCacheMarker, PackageResolverAdminMarker,
        PackageResolverAdminProxy, PackageResolverMarker, PackageResolverProxy,
        RepositoryManagerMarker, RepositoryManagerProxy, UpdatePolicy,
    },
    fidl_fuchsia_pkg_ext::{BlobId, RepositoryConfig, RepositoryConfigBuilder, RepositoryConfigs},
    fidl_fuchsia_pkg_rewrite::{
        EngineMarker as RewriteEngineMarker, EngineProxy as RewriteEngineProxy,
    },
    fidl_fuchsia_pkg_rewrite_ext::{Rule, RuleConfig},
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_inspect::reader::{self, NodeHierarchy},
    fuchsia_merkle::{Hash, MerkleTree},
    fuchsia_pkg_testing::{serve::ServedRepository, Package, PackageBuilder},
    fuchsia_url::pkg_url::RepoUrl,
    fuchsia_zircon::{self as zx, Status},
    futures::prelude::*,
    pkgfs_ramdisk::PkgfsRamdisk,
    serde::Serialize,
    std::{
        convert::TryInto,
        fs::File,
        io::{self, BufWriter, Read},
        sync::Arc,
    },
    tempfile::TempDir,
};

pub mod mock_filesystem;

pub trait PkgFs {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error>;
}

impl PkgFs for PkgfsRamdisk {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        PkgfsRamdisk::root_dir_handle(self)
    }
}

pub struct Mounts {
    pkg_resolver_data: DirOrProxy,
    pkg_resolver_config_data: DirOrProxy,
}

#[derive(Serialize)]
pub struct Config {
    pub enable_dynamic_configuration: bool,
}

#[derive(Default)]
pub struct MountsBuilder {
    pkg_resolver_data: Option<DirOrProxy>,
    pkg_resolver_config_data: Option<DirOrProxy>,
    config: Option<Config>,
    static_repository: Option<RepositoryConfig>,
    dynamic_rewrite_rules: Option<RuleConfig>,
    dynamic_repositories: Option<RepositoryConfigs>,
}

impl MountsBuilder {
    pub fn new() -> Self {
        Self::default()
    }
    pub fn pkg_resolver_data(mut self, pkg_resolver_data: DirOrProxy) -> Self {
        self.pkg_resolver_data = Some(pkg_resolver_data);
        self
    }
    pub fn pkg_resolver_config_data(mut self, pkg_resolver_config_data: DirOrProxy) -> Self {
        self.pkg_resolver_config_data = Some(pkg_resolver_config_data);
        self
    }
    pub fn config(mut self, config: Config) -> Self {
        self.config = Some(config);
        self
    }
    pub fn static_repository(mut self, static_repository: RepositoryConfig) -> Self {
        self.static_repository = Some(static_repository);
        self
    }
    pub fn dynamic_rewrite_rules(mut self, dynamic_rewrite_rules: RuleConfig) -> Self {
        self.dynamic_rewrite_rules = Some(dynamic_rewrite_rules);
        self
    }
    pub fn dynamic_repositories(mut self, dynamic_repositories: RepositoryConfigs) -> Self {
        self.dynamic_repositories = Some(dynamic_repositories);
        self
    }
    pub fn build(self) -> Mounts {
        let mounts = Mounts {
            pkg_resolver_data: self
                .pkg_resolver_data
                .unwrap_or(DirOrProxy::Dir(tempfile::tempdir().expect("/tmp to exist"))),
            pkg_resolver_config_data: self
                .pkg_resolver_config_data
                .unwrap_or(DirOrProxy::Dir(tempfile::tempdir().expect("/tmp to exist"))),
        };
        if let Some(config) = self.config {
            mounts.add_config(&config);
        }
        if let Some(config) = self.static_repository {
            mounts.add_static_repository(config);
        }
        if let Some(config) = self.dynamic_rewrite_rules {
            mounts.add_dynamic_rewrite_rules(&config);
        }
        if let Some(config) = self.dynamic_repositories {
            mounts.add_dynamic_repositories(&config);
        }
        mounts
    }
}

impl Mounts {
    fn add_config(&self, config: &Config) {
        if let DirOrProxy::Dir(ref d) = self.pkg_resolver_config_data {
            let f = File::create(d.path().join("config.json")).unwrap();
            serde_json::to_writer(BufWriter::new(f), &config).unwrap();
        } else {
            panic!("not supported");
        }
    }

    fn add_static_repository(&self, config: RepositoryConfig) {
        if let DirOrProxy::Dir(ref d) = self.pkg_resolver_config_data {
            let static_repo_path = d.path().join("repositories");
            if !static_repo_path.exists() {
                std::fs::create_dir(&static_repo_path).unwrap();
            }
            let f =
                File::create(static_repo_path.join(format!("{}.json", config.repo_url().host())))
                    .unwrap();
            serde_json::to_writer(BufWriter::new(f), &RepositoryConfigs::Version1(vec![config]))
                .unwrap();
        } else {
            panic!("not supported");
        }
    }

    fn add_dynamic_rewrite_rules(&self, rule_config: &RuleConfig) {
        if let DirOrProxy::Dir(ref d) = self.pkg_resolver_data {
            let f = File::create(d.path().join("rewrites.json")).unwrap();
            serde_json::to_writer(BufWriter::new(f), rule_config).unwrap();
        } else {
            panic!("not supported");
        }
    }
    fn add_dynamic_repositories(&self, repo_configs: &RepositoryConfigs) {
        if let DirOrProxy::Dir(ref d) = self.pkg_resolver_data {
            let f = File::create(d.path().join("repositories.json")).unwrap();
            serde_json::to_writer(BufWriter::new(f), repo_configs).unwrap();
        } else {
            panic!("not supported");
        }
    }
}

pub enum DirOrProxy {
    Dir(TempDir),
    Proxy(DirectoryProxy),
}

pub trait AppBuilderExt {
    fn add_dir_or_proxy_to_namespace(
        self,
        path: impl Into<String>,
        dir_or_proxy: &DirOrProxy,
    ) -> Self;
}

impl AppBuilderExt for AppBuilder {
    fn add_dir_or_proxy_to_namespace(
        self,
        path: impl Into<String>,
        dir_or_proxy: &DirOrProxy,
    ) -> Self {
        match dir_or_proxy {
            DirOrProxy::Dir(d) => {
                self.add_dir_to_namespace(path.into(), File::open(d.path()).unwrap()).unwrap()
            }
            DirOrProxy::Proxy(p) => {
                self.add_handle_to_namespace(path.into(), clone_directory_proxy(p))
            }
        }
    }
}

pub fn clone_directory_proxy(proxy: &DirectoryProxy) -> zx::Handle {
    let (client, server) = fidl::endpoints::create_endpoints().unwrap();
    proxy.clone(CLONE_FLAG_SAME_RIGHTS, server).unwrap();
    client.into()
}

pub struct TestEnvBuilder<PkgFsFn, P, MountsFn>
where
    PkgFsFn: FnOnce() -> P,
{
    pkgfs: PkgFsFn,
    mounts: MountsFn,
    boot_arguments_service: Option<BootArgumentsService<'static>>,
}

impl TestEnvBuilder<fn() -> PkgfsRamdisk, PkgfsRamdisk, fn() -> Mounts> {
    pub fn new() -> Self {
        Self {
            pkgfs: || PkgfsRamdisk::start().expect("pkgfs to start"),
            // If it's not overriden, the default state of the mounts allows for dynamic configuration.
            // We do this because in the majority of tests, we'll want to use dynamic repos and rewrite rules.
            // Note: this means that we'll produce different envs from TestEnvBuilder::new().build()
            // vs TestEnvBuilder::new().mounts(MountsBuilder::new().build()).build()
            mounts: || {
                MountsBuilder::new().config(Config { enable_dynamic_configuration: true }).build()
            },
            boot_arguments_service: None,
        }
    }
}

impl<PkgFsFn, P, MountsFn> TestEnvBuilder<PkgFsFn, P, MountsFn>
where
    PkgFsFn: FnOnce() -> P,
    P: PkgFs,
    MountsFn: FnOnce() -> Mounts,
{
    pub fn build(self) -> TestEnv<P> {
        TestEnv::new_with_pkg_fs_and_mounts_and_arguments_service(
            (self.pkgfs)(),
            (self.mounts)(),
            self.boot_arguments_service,
        )
    }
    pub fn pkgfs<Pother>(
        self,
        pkgfs: Pother,
    ) -> TestEnvBuilder<impl FnOnce() -> Pother, Pother, MountsFn>
    where
        Pother: PkgFs + 'static,
    {
        TestEnvBuilder::<_, Pother, MountsFn> {
            pkgfs: || pkgfs,
            mounts: self.mounts,
            boot_arguments_service: self.boot_arguments_service,
        }
    }
    pub fn mounts(self, mounts: Mounts) -> TestEnvBuilder<PkgFsFn, P, impl FnOnce() -> Mounts> {
        TestEnvBuilder::<PkgFsFn, P, _> {
            pkgfs: self.pkgfs,
            mounts: || mounts,
            boot_arguments_service: self.boot_arguments_service,
        }
    }
    pub fn boot_arguments_service(
        self,
        svc: BootArgumentsService<'static>,
    ) -> TestEnvBuilder<PkgFsFn, P, MountsFn> {
        TestEnvBuilder::<PkgFsFn, P, _> {
            pkgfs: self.pkgfs,
            mounts: self.mounts,
            boot_arguments_service: Some(svc),
        }
    }
}

pub struct Apps {
    pub pkg_cache: App,
    pub pkg_resolver: App,
}

pub struct Proxies {
    pub resolver_admin: PackageResolverAdminProxy,
    pub resolver: PackageResolverProxy,
    pub repo_manager: RepositoryManagerProxy,
    pub rewrite_engine: RewriteEngineProxy,
}

impl Proxies {
    pub fn from_app(app: &App) -> Self {
        Proxies {
            resolver: app
                .connect_to_service::<PackageResolverMarker>()
                .expect("connect to package resolver"),
            resolver_admin: app
                .connect_to_service::<PackageResolverAdminMarker>()
                .expect("connect to package resolver admin"),
            repo_manager: app
                .connect_to_service::<RepositoryManagerMarker>()
                .expect("connect to repository manager"),
            rewrite_engine: app
                .connect_to_service::<RewriteEngineMarker>()
                .expect("connect to rewrite engine"),
        }
    }
}

pub struct TestEnv<P = PkgfsRamdisk> {
    pub pkgfs: P,
    pub env: NestedEnvironment,
    pub apps: Apps,
    pub proxies: Proxies,
    pub _mounts: Mounts,
    pub nested_environment_label: String,
}

impl TestEnv<PkgfsRamdisk> {
    pub fn add_slice_to_blobfs(&self, slice: &[u8]) {
        let merkle = MerkleTree::from_reader(slice).expect("merkle slice").root().to_string();
        let mut blob = self
            .pkgfs
            .blobfs()
            .root_dir()
            .expect("blobfs has root dir")
            .write_file(merkle, 0)
            .expect("create file in blobfs");
        blob.set_len(slice.len() as u64).expect("set_len");
        io::copy(&mut &slice[..], &mut blob).expect("copy from slice to blob");
    }

    pub fn add_file_with_merkle_to_blobfs(&self, mut file: File, merkle: &Hash) {
        let mut blob = self
            .pkgfs
            .blobfs()
            .root_dir()
            .expect("blobfs has root dir")
            .write_file(merkle.to_string(), 0)
            .expect("create file in blobfs");
        blob.set_len(file.metadata().expect("file has metadata").len()).expect("set_len");
        io::copy(&mut file, &mut blob).expect("copy file to blobfs");
    }

    pub fn add_file_to_pkgfs_at_path(&self, mut file: File, path: impl openat::AsPath) {
        let mut blob = self
            .pkgfs
            .root_dir()
            .expect("pkgfs root_dir")
            .new_file(path, 0)
            .expect("create file in pkgfs");
        blob.set_len(file.metadata().expect("file has metadata").len()).expect("set_len");
        io::copy(&mut file, &mut blob).expect("copy file to pkgfs");
    }

    pub fn partially_add_file_to_pkgfs_at_path(&self, mut file: File, path: impl openat::AsPath) {
        let full_len = file.metadata().expect("file has metadata").len();
        assert!(full_len > 1, "can't partially write 1 byte");
        let mut partial_bytes = vec![0; full_len as usize / 2];
        file.read_exact(partial_bytes.as_mut_slice()).expect("partial read of file");
        let mut blob = self
            .pkgfs
            .root_dir()
            .expect("pkgfs root_dir")
            .new_file(path, 0)
            .expect("create file in pkgfs");
        blob.set_len(full_len).expect("set_len");
        io::copy(&mut partial_bytes.as_slice(), &mut blob).expect("copy file to pkgfs");
    }

    pub fn partially_add_slice_to_pkgfs_at_path(&self, slice: &[u8], path: impl openat::AsPath) {
        assert!(slice.len() > 1, "can't partially write 1 byte");
        let partial_slice = &slice[0..slice.len() / 2];
        let mut blob = self
            .pkgfs
            .root_dir()
            .expect("pkgfs root_dir")
            .new_file(path, 0)
            .expect("create file in pkgfs");
        blob.set_len(slice.len() as u64).expect("set_len");
        io::copy(&mut &partial_slice[..], &mut blob).expect("copy file to pkgfs");
    }

    pub async fn stop(self) {
        // Tear down the environment in reverse order, ending with the storage.
        drop(self.proxies);
        drop(self.apps);
        drop(self.env);
        self.pkgfs.stop().await.expect("pkgfs to stop gracefully");
    }
}

pub struct BootArgumentsService<'a> {
    tuf_repo_config: &'a str,
}
impl BootArgumentsService<'_> {
    pub fn new(tuf_repo_config: &'static str) -> Self {
        Self { tuf_repo_config }
    }
    async fn run_service(self: Arc<Self>, mut stream: ArgumentsRequestStream) {
        while let Some(req) = stream.try_next().await.unwrap() {
            match req {
                ArgumentsRequest::GetString { key, responder } => {
                    assert_eq!(key, "tuf_repo_config", "Unexpected GetString key: {}", key);
                    responder.send(Some(self.tuf_repo_config)).unwrap();
                }
                _ => panic!("Unexpected request to mock BootArgumentsService!"),
            };
        }
    }
}

impl<P: PkgFs> TestEnv<P> {
    fn new_with_pkg_fs_and_mounts_and_arguments_service(
        pkgfs: P,
        mounts: Mounts,
        boot_arguments_service: Option<BootArgumentsService<'static>>,
    ) -> Self {
        let mut pkg_cache = AppBuilder::new(
            "fuchsia-pkg://fuchsia.com/pkg-resolver-integration-tests#meta/pkg-cache.cmx"
                .to_owned(),
        )
        .add_handle_to_namespace(
            "/pkgfs".to_owned(),
            pkgfs.root_dir_handle().expect("pkgfs dir to open").into(),
        );

        let pkg_resolver = AppBuilder::new(RESOLVER_MANIFEST_URL.to_owned())
            .add_handle_to_namespace(
                "/pkgfs".to_owned(),
                pkgfs.root_dir_handle().expect("pkgfs dir to open").into(),
            )
            .add_dir_or_proxy_to_namespace("/data", &mounts.pkg_resolver_data)
            .add_dir_or_proxy_to_namespace("/config/data", &mounts.pkg_resolver_config_data)
            .add_dir_to_namespace("/config/ssl".to_owned(), File::open("/pkg/data/ssl").unwrap())
            .unwrap();

        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_net::NameLookupMarker, _>()
            .add_proxy_service::<fidl_fuchsia_posix_socket::ProviderMarker, _>()
            .add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, _>()
            .add_proxy_service::<fidl_fuchsia_tracing_provider::RegistryMarker, _>()
            .add_proxy_service_to::<PackageCacheMarker, _>(
                pkg_cache.directory_request().unwrap().clone(),
            );

        if let Some(boot_arguments_service) = boot_arguments_service {
            let mock_arg_svc = Arc::new(boot_arguments_service);
            fs.add_fidl_service(move |stream: ArgumentsRequestStream| {
                fasync::spawn(Arc::clone(&mock_arg_svc).run_service(stream));
            });
        }

        let mut salt = [0; 4];
        zx::cprng_draw(&mut salt[..]).expect("zx_cprng_draw does not fail");
        let environment_label = format!("pkg-resolver-env_{}", hex::encode(&salt));
        let env = fs
            .create_nested_environment(&environment_label)
            .expect("nested environment to create successfully");
        fasync::spawn(fs.collect());

        let pkg_cache = pkg_cache.spawn(env.launcher()).expect("package cache to launch");
        let pkg_resolver = pkg_resolver.spawn(env.launcher()).expect("package resolver to launch");

        Self {
            env,
            pkgfs,
            proxies: Proxies::from_app(&pkg_resolver),
            apps: Apps { pkg_cache: pkg_cache, pkg_resolver },
            _mounts: mounts,
            nested_environment_label: environment_label,
        }
    }

    pub async fn set_experiment_state(&self, experiment: Experiment, state: bool) {
        self.proxies
            .resolver_admin
            .set_experiment_state(experiment, state)
            .await
            .expect("experiment state to toggle");
    }

    pub async fn register_repo(&self, repo: &ServedRepository) {
        self.register_repo_at_url(repo, "fuchsia-pkg://test").await;
    }

    pub async fn register_repo_at_url<R>(&self, repo: &ServedRepository, url: R)
    where
        R: TryInto<RepoUrl>,
        R::Error: std::fmt::Debug,
    {
        let repo_config = repo.make_repo_config(url.try_into().unwrap());
        self.proxies.repo_manager.add(repo_config.into()).await.unwrap();
    }

    pub async fn restart_pkg_resolver(&mut self) {
        // Start a new package resolver component
        let pkg_resolver = AppBuilder::new(RESOLVER_MANIFEST_URL.to_owned())
            .add_handle_to_namespace(
                "/pkgfs".to_owned(),
                self.pkgfs.root_dir_handle().expect("pkgfs dir to open").into(),
            )
            .add_dir_or_proxy_to_namespace("/data", &self._mounts.pkg_resolver_data)
            .add_dir_or_proxy_to_namespace("/config/data", &self._mounts.pkg_resolver_config_data)
            .add_dir_to_namespace("/config/ssl".to_owned(), File::open("/pkg/data/ssl").unwrap())
            .unwrap();
        let pkg_resolver =
            pkg_resolver.spawn(self.env.launcher()).expect("package resolver to launch");

        // Previous pkg-resolver terminated when its app goes out of scope
        self.proxies = Proxies::from_app(&pkg_resolver);
        self.apps.pkg_resolver = pkg_resolver;

        self.wait_for_pkg_resolver_to_start().await;
    }

    pub async fn wait_for_pkg_resolver_to_start(&self) {
        self.proxies
            .rewrite_engine
            .test_apply("fuchsia-pkg://test")
            .await
            .expect("fidl call succeeds")
            .expect("test apply result is ok");
    }

    pub fn connect_to_resolver(&self) -> PackageResolverProxy {
        self.apps
            .pkg_resolver
            .connect_to_service::<PackageResolverMarker>()
            .expect("connect to package resolver")
    }

    pub fn resolve_package(
        &self,
        url: &str,
    ) -> impl Future<Output = Result<DirectoryProxy, Status>> {
        resolve_package(&self.proxies.resolver, url)
    }

    pub fn get_hash(&self, url: impl Into<String>) -> impl Future<Output = Result<BlobId, Status>> {
        let fut =
            self.proxies.resolver.get_hash(&mut fidl_fuchsia_pkg::PackageUrl { url: url.into() });
        async move { fut.await.unwrap().map(|blob_id| blob_id.into()).map_err(|i| Status::from_raw(i)) }
    }

    pub async fn pkg_resolver_inspect_hierarchy(&self) -> NodeHierarchy {
        let pattern = format!(
            "/hub/r/{}/*/c/pkg-resolver.cmx/*/out/diagnostics/{}",
            glob::Pattern::escape(&self.nested_environment_label),
            TreeMarker::SERVICE_NAME,
        );
        let paths = glob::glob_with(
            &pattern,
            glob::MatchOptions {
                case_sensitive: true,
                require_literal_separator: true,
                require_literal_leading_dot: false,
            },
        )
        .expect("glob pattern successfully compiles");
        let mut paths = paths.collect::<Result<Vec<_>, _>>().unwrap();
        assert_eq!(paths.len(), 1, "{:?}", paths);
        let path = paths.pop().unwrap();

        let (tree, server_end) =
            fidl::endpoints::create_proxy::<TreeMarker>().expect("failed to create Tree proxy");
        fdio::service_connect(&path.to_string_lossy().to_string(), server_end.into_channel())
            .expect("failed to connect to Tree service");
        reader::read_from_tree(&tree).await.expect("failed to get inspect hierarchy")
    }
}

pub const EMPTY_REPO_PATH: &str = "/pkg/empty-repo";
const RESOLVER_MANIFEST_URL: &str =
    "fuchsia-pkg://fuchsia.com/pkg-resolver-integration-tests#meta/pkg-resolver.cmx";

// The following functions generate unique test package dummy content. Callers are recommended
// to pass in the name of the test case.
pub fn test_package_bin(s: &str) -> Vec<u8> {
    return format!("!/boot/bin/sh\n{}", s).as_bytes().to_owned();
}

pub fn test_package_cmx(s: &str) -> Vec<u8> {
    return format!("\"{{\"program\":{{\"binary\":\"bin/{}\"}}", s).as_bytes().to_owned();
}

pub fn extra_blob_contents(s: &str, i: u32) -> Vec<u8> {
    format!("contents of file {}-{}", s, i).as_bytes().to_owned()
}

pub async fn make_pkg_with_extra_blobs(s: &str, n: u32) -> Package {
    let mut pkg = PackageBuilder::new(s)
        .add_resource_at(format!("bin/{}", s), &test_package_bin(s)[..])
        .add_resource_at(format!("meta/{}.cmx", s), &test_package_cmx(s)[..]);
    for i in 0..n {
        pkg =
            pkg.add_resource_at(format!("data/{}-{}", s, i), extra_blob_contents(s, i).as_slice());
    }
    pkg.build().await.unwrap()
}

pub fn resolve_package(
    resolver: &PackageResolverProxy,
    url: &str,
) -> impl Future<Output = Result<DirectoryProxy, Status>> {
    let (package, package_server_end) = fidl::endpoints::create_proxy().unwrap();
    let selectors: Vec<&str> = vec![];
    let status_fut = resolver.resolve(
        url,
        &mut selectors.into_iter(),
        &mut UpdatePolicy { fetch_if_absent: true, allow_old_versions: false },
        package_server_end,
    );
    async move {
        let status = status_fut.await.expect("package resolve fidl call");
        Status::ok(status)?;
        Ok(package)
    }
}

pub fn make_repo_config(repo: &RepositoryConfig) -> RepositoryConfigs {
    RepositoryConfigs::Version1(vec![repo.clone()])
}

pub fn make_repo() -> RepositoryConfig {
    RepositoryConfigBuilder::new("fuchsia-pkg://example.com".parse().unwrap()).build()
}

pub async fn get_repos(repository_manager: &RepositoryManagerProxy) -> Vec<RepositoryConfig> {
    let (repo_iterator, repo_iterator_server) =
        fidl::endpoints::create_proxy().expect("create repo iterator proxy");
    repository_manager.list(repo_iterator_server).expect("list repos");
    let mut ret = vec![];
    loop {
        let repos = repo_iterator.next().await.expect("advance repo iterator");
        if repos.is_empty() {
            return ret;
        }
        ret.extend(repos.into_iter().map(|r| r.try_into().unwrap()))
    }
}

pub async fn get_rules(rewrite_engine: &RewriteEngineProxy) -> Vec<Rule> {
    let (rule_iterator, rule_iterator_server) =
        fidl::endpoints::create_proxy().expect("create rule iterator proxy");
    rewrite_engine.list(rule_iterator_server).expect("list rules");
    let mut ret = vec![];
    loop {
        let rules = rule_iterator.next().await.expect("advance rule iterator");
        if rules.is_empty() {
            return ret;
        }
        ret.extend(rules.into_iter().map(|r| r.try_into().unwrap()))
    }
}
