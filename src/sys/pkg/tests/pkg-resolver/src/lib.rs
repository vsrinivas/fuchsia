// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_boot::{ArgumentsRequest, ArgumentsRequestStream},
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
    fuchsia_inspect::reader::{NodeHierarchy, PartialNodeHierarchy},
    fuchsia_merkle::{Hash, MerkleTree},
    fuchsia_pkg_testing::{serve::ServedRepository, Package, PackageBuilder},
    fuchsia_zircon::{self as zx, Status},
    futures::prelude::*,
    pkgfs_ramdisk::PkgfsRamdisk,
    serde_derive::Serialize,
    std::{
        convert::TryFrom,
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
    pub pkg_resolver_data: DirOrProxy,
    pub pkg_resolver_config_data: DirOrProxy,
}

#[derive(Serialize)]
pub struct Config {
    pub disable_dynamic_configuration: bool,
}

impl Mounts {
    pub fn new() -> Self {
        Self {
            pkg_resolver_data: DirOrProxy::Dir(tempfile::tempdir().expect("/tmp to exist")),
            pkg_resolver_config_data: DirOrProxy::Dir(tempfile::tempdir().expect("/tmp to exist")),
        }
    }
    pub fn add_config(&self, config: &Config) {
        if let DirOrProxy::Dir(ref d) = self.pkg_resolver_config_data {
            let f = File::create(d.path().join("config.json")).unwrap();
            serde_json::to_writer(BufWriter::new(f), &config).unwrap();
        } else {
            panic!("not supported");
        }
    }

    pub fn add_static_repository(&self, config: RepositoryConfig) {
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

    pub fn add_dynamic_rewrite_rules(&self, rule_config: &RuleConfig) {
        if let DirOrProxy::Dir(ref d) = self.pkg_resolver_data {
            let f = File::create(d.path().join("rewrites.json")).unwrap();
            serde_json::to_writer(BufWriter::new(f), rule_config).unwrap();
        } else {
            panic!("not supported");
        }
    }
    pub fn add_dynamic_repositories(&self, repo_configs: &RepositoryConfigs) {
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
            mounts: || Mounts::new(),
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
    args: &'a [u8],
}
impl BootArgumentsService<'_> {
    pub fn new(args: &'static [u8]) -> Self {
        Self { args }
    }
    async fn run_service(self: Arc<Self>, mut stream: ArgumentsRequestStream) {
        while let Some(req) = stream.try_next().await.unwrap() {
            match req {
                ArgumentsRequest::Get { responder } => {
                    let size = self.args.len() as u64;
                    let vmo = fuchsia_zircon::Vmo::create(size).unwrap();
                    vmo.write(self.args, 0).unwrap();
                    responder.send(vmo, size).unwrap();
                }
            }
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
        let repo_url = "fuchsia-pkg://test".parse().unwrap();
        let repo_config = repo.make_repo_config(repo_url);

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
        // When `glob` is matching a path component that is a string literal, it uses
        // `std::fs::metadata()` to test the existence of the path instead of listing the parent dir.
        // `metadata()` calls `stat`, which creates and destroys an fd in fdio.
        // When the fd is for "root.inspect", which is a VMO, destroying the fd calls
        // `zxio_vmofile_release`, which makes a fuchsia.io.File.Seek FIDL call.
        // This FIDL call is received by `ServiceFs`, which, b/c "root.inspect" was opened
        // by fdio with `OPEN_FLAG_NODE_REFERENCE`, is treating the zircon channel as a stream of
        // Node requests.
        // `ServiceFs` then closes the channel and logs a
        // "ServiceFs failed to parse an incoming node request: UnknownOrdinal" error (with
        // the File.Seek ordinal).
        // `ServiceFs` closing the channel is seen by `metadata` as a `BrokenPipe` error, which
        // `glob` interprets as there being nothing at "root.inspect", so the VMO is not found.
        // To work around this, we use a trivial pattern in the "root.inspect" path component,
        // which prevents the `metadata` shortcut.
        //
        // To fix this, `zxio_vmofile_release` probably shouldn't be unconditionally calling
        // `fuchsia.io.File.Seek`, because, per a comment in `io.fidl`, that is not a valid
        // method to be called on a `Node` opened with `OPEN_FLAG_NODE_REFERENCE`.
        // `zxio_vmofile_release` could determine if the `Node` were opened with
        // `OPEN_FLAG_NODE_REFERENCE` (by calling `Node.NodeGetFlags` or `File.GetFlags`).
        // Note that if `zxio_vmofile_release` starts calling `File.GetFlags`, `ServiceFs`
        // will need to stop unconditionally treating `Node`s opened with `OPEN_FLAG_NODE_REFERNCE`
        // as `Node`s.
        // TODO(fxb/40888)
        let pattern = format!(
            "/hub/r/{}/*/c/pkg-resolver.cmx/*/out/diagnostics/root.i[n]spect",
            glob::Pattern::escape(&self.nested_environment_label)
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

        let vmo_file = File::open(path).expect("file exists");
        let vmo = fdio::get_vmo_copy_from_file(&vmo_file).expect("vmo exists");

        PartialNodeHierarchy::try_from(&vmo).unwrap().into()
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
