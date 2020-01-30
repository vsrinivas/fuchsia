// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::Error,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_amber::ControlMarker as AmberMarker,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, CLONE_FLAG_SAME_RIGHTS},
    fidl_fuchsia_pkg::{
        ExperimentToggle as Experiment, PackageCacheMarker, PackageResolverAdminMarker,
        PackageResolverAdminProxy, PackageResolverMarker, PackageResolverProxy,
        RepositoryManagerMarker, RepositoryManagerProxy, UpdatePolicy,
    },
    fidl_fuchsia_pkg_rewrite::{
        EngineMarker as RewriteEngineMarker, EngineProxy as RewriteEngineProxy,
    },
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_inspect::reader::NodeHierarchy,
    fuchsia_pkg_testing::{
        get_inspect_hierarchy, serve::ServedRepository, Package, PackageBuilder,
    },
    fuchsia_zircon::{self as zx, Status},
    futures::prelude::*,
    pkgfs_ramdisk::PkgfsRamdisk,
    serde_derive::Serialize,
    std::{fs::File, io::BufWriter},
    tempfile::TempDir,
};

mod dynamic_repositories_disabled;
mod dynamic_rewrite_disabled;
mod inspect;
mod mock_filesystem;
mod resolve_propagates_pkgfs_failure;
mod resolve_recovers_from_http_errors;
mod resolve_succeeds;
mod resolve_succeeds_with_broken_minfs;

trait PkgFs {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error>;
}

impl PkgFs for PkgfsRamdisk {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        PkgfsRamdisk::root_dir_handle(self)
    }
}

struct Mounts {
    pkg_resolver_data: DirOrProxy,
    pkg_resolver_config_data: DirOrProxy,
}

enum DirOrProxy {
    Dir(TempDir),
    Proxy(DirectoryProxy),
}

trait AppBuilderExt {
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

fn clone_directory_proxy(proxy: &DirectoryProxy) -> zx::Handle {
    let (client, server) = fidl::endpoints::create_endpoints().unwrap();
    proxy.clone(CLONE_FLAG_SAME_RIGHTS, server).unwrap();
    client.into()
}

#[derive(Serialize)]
struct Config {
    disable_dynamic_configuration: bool,
}

impl Mounts {
    fn new() -> Self {
        Self {
            pkg_resolver_data: DirOrProxy::Dir(tempfile::tempdir().expect("/tmp to exist")),
            pkg_resolver_config_data: DirOrProxy::Dir(tempfile::tempdir().expect("/tmp to exist")),
        }
    }
    fn add_config(&self, config: &Config) {
        if let DirOrProxy::Dir(ref d) = self.pkg_resolver_config_data {
            let f = File::create(d.path().join("config.json")).unwrap();
            serde_json::to_writer(BufWriter::new(f), &config).unwrap();
        } else {
            panic!("not supported");
        }
    }
}

struct TestEnvBuilder<PkgFsFn, P, MountsFn>
where
    PkgFsFn: FnOnce() -> P,
{
    include_amber: bool,
    pkgfs: PkgFsFn,
    mounts: MountsFn,
}

impl TestEnvBuilder<fn() -> PkgfsRamdisk, PkgfsRamdisk, fn() -> Mounts> {
    fn new() -> Self {
        Self {
            include_amber: true,
            pkgfs: || PkgfsRamdisk::start().expect("pkgfs to start"),
            mounts: || Mounts::new(),
        }
    }
}

impl<PkgFsFn, P, MountsFn> TestEnvBuilder<PkgFsFn, P, MountsFn>
where
    PkgFsFn: FnOnce() -> P,
    P: PkgFs,
    MountsFn: FnOnce() -> Mounts,
{
    fn build(self) -> TestEnv<P> {
        TestEnv::new_with_pkg_fs_and_mounts((self.pkgfs)(), (self.mounts)(), self.include_amber)
    }
    fn include_amber(mut self, include_amber: bool) -> Self {
        self.include_amber = include_amber;
        self
    }
    fn pkgfs<Pother>(
        self,
        pkgfs: Pother,
    ) -> TestEnvBuilder<impl FnOnce() -> Pother, Pother, MountsFn>
    where
        Pother: PkgFs + 'static,
    {
        TestEnvBuilder::<_, Pother, MountsFn> {
            include_amber: self.include_amber,
            pkgfs: || pkgfs,
            mounts: self.mounts,
        }
    }
    fn mounts(self, mounts: Mounts) -> TestEnvBuilder<PkgFsFn, P, impl FnOnce() -> Mounts> {
        TestEnvBuilder::<PkgFsFn, P, _> {
            include_amber: self.include_amber,
            pkgfs: self.pkgfs,
            mounts: || mounts,
        }
    }
}

struct Apps {
    _amber: App,
    _pkg_cache: App,
    pkg_resolver: App,
}

struct Proxies {
    resolver_admin: PackageResolverAdminProxy,
    resolver: PackageResolverProxy,
    repo_manager: RepositoryManagerProxy,
    rewrite_engine: RewriteEngineProxy,
}

impl Proxies {
    fn from_app(app: &App) -> Self {
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

struct TestEnv<P = PkgfsRamdisk> {
    pkgfs: P,
    env: NestedEnvironment,
    apps: Apps,
    proxies: Proxies,
    _mounts: Mounts,
    nested_environment_label: String,
}

impl TestEnv<PkgfsRamdisk> {
    // workaround for fxb/38162
    async fn stop(self) {
        // Tear down the environment in reverse order, ending with the storage.
        drop(self.proxies);
        drop(self.apps);
        drop(self.env);
        self.pkgfs.stop().await.expect("pkgfs to stop gracefully");
    }
}

impl<P: PkgFs> TestEnv<P> {
    fn new_with_pkg_fs_and_mounts(pkgfs: P, mounts: Mounts, include_amber: bool) -> Self {
        let mut amber = AppBuilder::new(
            "fuchsia-pkg://fuchsia.com/pkg-resolver-integration-tests#meta/amber.cmx",
        )
        .add_handle_to_namespace(
            "/pkgfs".to_owned(),
            pkgfs.root_dir_handle().expect("pkgfs dir to open").into(),
        );

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
            .add_dir_or_proxy_to_namespace("/config/data", &mounts.pkg_resolver_config_data);

        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_net::NameLookupMarker, _>()
            .add_proxy_service::<fidl_fuchsia_posix_socket::ProviderMarker, _>()
            .add_proxy_service::<fidl_fuchsia_tracing_provider::RegistryMarker, _>()
            .add_proxy_service_to::<PackageCacheMarker, _>(
                pkg_cache.directory_request().unwrap().clone(),
            );

        if include_amber {
            fs.add_proxy_service_to::<AmberMarker, _>(amber.directory_request().unwrap().clone());
        }

        let mut salt = [0; 4];
        zx::cprng_draw(&mut salt[..]).expect("zx_cprng_draw does not fail");
        let environment_label = format!("pkg-resolver-env_{}", hex::encode(&salt));
        let env = fs
            .create_nested_environment(&environment_label)
            .expect("nested environment to create successfully");
        fasync::spawn(fs.collect());

        let amber = amber.spawn(env.launcher()).expect("amber to launch");
        let pkg_cache = pkg_cache.spawn(env.launcher()).expect("package cache to launch");
        let pkg_resolver = pkg_resolver.spawn(env.launcher()).expect("package resolver to launch");

        Self {
            env,
            pkgfs,
            proxies: Proxies::from_app(&pkg_resolver),
            apps: Apps { _amber: amber, _pkg_cache: pkg_cache, pkg_resolver },
            _mounts: mounts,
            nested_environment_label: environment_label,
        }
    }

    async fn set_experiment_state(&self, experiment: Experiment, state: bool) {
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
            .add_dir_or_proxy_to_namespace("/config/data", &self._mounts.pkg_resolver_config_data);
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

    fn connect_to_resolver(&self) -> PackageResolverProxy {
        self.apps
            .pkg_resolver
            .connect_to_service::<PackageResolverMarker>()
            .expect("connect to package resolver")
    }

    fn resolve_package(&self, url: &str) -> impl Future<Output = Result<DirectoryProxy, Status>> {
        resolve_package(&self.proxies.resolver, url)
    }

    async fn pkg_resolver_inspect_hierarchy(&self) -> NodeHierarchy {
        get_inspect_hierarchy(&self.nested_environment_label, "pkg-resolver.cmx").await
    }
}

const EMPTY_REPO_PATH: &str = "/pkg/empty-repo";
const RESOLVER_MANIFEST_URL: &str =
    "fuchsia-pkg://fuchsia.com/pkg-resolver-integration-tests#meta/pkg-resolver.cmx";

// The following functions generate unique test package dummy content. Callers are recommended
// to pass in the name of the test case.
fn test_package_bin(s: &str) -> Vec<u8> {
    return format!("!/boot/bin/sh\n{}", s).as_bytes().to_owned();
}

fn test_package_cmx(s: &str) -> Vec<u8> {
    return format!("\"{{\"program\":{{\"binary\":\"bin/{}\"}}", s).as_bytes().to_owned();
}

fn extra_blob_contents(s: &str, i: u32) -> Vec<u8> {
    format!("contents of file {}-{}", s, i).as_bytes().to_owned()
}

async fn make_pkg_with_extra_blobs(s: &str, n: u32) -> Package {
    let mut pkg = PackageBuilder::new(s)
        .add_resource_at(format!("bin/{}", s), &test_package_bin(s)[..])
        .add_resource_at(format!("meta/{}.cmx", s), &test_package_cmx(s)[..]);
    for i in 0..n {
        pkg =
            pkg.add_resource_at(format!("data/{}-{}", s, i), extra_blob_contents(s, i).as_slice());
    }
    pkg.build().await.unwrap()
}

fn resolve_package(
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
