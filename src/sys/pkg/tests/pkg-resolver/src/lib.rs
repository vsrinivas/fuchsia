// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    assert_matches::assert_matches,
    blobfs_ramdisk::BlobfsRamdisk,
    cobalt_client::traits::AsEventCodes,
    diagnostics_hierarchy::{testing::TreeAssertion, DiagnosticsHierarchy},
    diagnostics_reader::{ArchiveReader, ComponentSelector, Inspect},
    fidl::endpoints::{ClientEnd, DiscoverableProtocolMarker as _},
    fidl_fuchsia_cobalt::{CobaltEvent, CountEvent, EventPayload},
    fidl_fuchsia_component::{RealmMarker, RealmProxy},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_pkg::{
        ExperimentToggle as Experiment, FontResolverMarker, FontResolverProxy, PackageCacheMarker,
        PackageResolverAdminMarker, PackageResolverAdminProxy, PackageResolverMarker,
        PackageResolverProxy, RepositoryManagerMarker, RepositoryManagerProxy,
    },
    fidl_fuchsia_pkg_ext::{BlobId, RepositoryConfig, RepositoryConfigBuilder, RepositoryConfigs},
    fidl_fuchsia_pkg_rewrite::{
        EngineMarker as RewriteEngineMarker, EngineProxy as RewriteEngineProxy,
    },
    fidl_fuchsia_pkg_rewrite_ext::{Rule, RuleConfig},
    fuchsia_async as fasync,
    fuchsia_component_test::{
        new::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route},
        ScopedInstance, ScopedInstanceFactory,
    },
    fuchsia_merkle::{Hash, MerkleTree},
    fuchsia_pkg_testing::SystemImageBuilder,
    fuchsia_pkg_testing::{serve::ServedRepository, Package, PackageBuilder, Repository},
    fuchsia_url::pkg_url::RepoUrl,
    fuchsia_zircon::{self as zx, Status},
    futures::{future::BoxFuture, prelude::*},
    mock_boot_arguments::MockBootArgumentsService,
    parking_lot::Mutex,
    pkgfs_ramdisk::PkgfsRamdisk,
    serde::Serialize,
    std::{
        collections::HashMap,
        convert::TryInto,
        fs::File,
        io::{self, BufWriter, Read, Write},
        path::{Path, PathBuf},
        sync::Arc,
        time::Duration,
    },
    tempfile::TempDir,
    vfs::directory::{entry::DirectoryEntry as _, helper::DirectlyMutable as _},
};

// If the body of an https response is not large enough, hyper will download the body
// along with the header in the initial fuchsia_hyper::HttpsClient.request(). This means
// that even if the body is implemented with a stream that sends some bytes and then fails
// before the transfer is complete, the error will occur on the initial request instead
// of when looping over the Response body bytes.
// This value probably just needs to be larger than the Hyper buffer, which defaults to 400 kB
// https://docs.rs/hyper/0.13.10/hyper/client/struct.Builder.html#method.http1_max_buf_size
pub const FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING: usize = 600_000;

pub mod mock_filesystem;

pub trait PkgFs {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error>;

    fn blobfs_root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error>;

    fn system_image_hash(&self) -> Option<Hash>;
}

impl PkgFs for PkgfsRamdisk {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        PkgfsRamdisk::root_dir_handle(self)
    }

    fn blobfs_root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        self.blobfs().root_dir_handle()
    }

    fn system_image_hash(&self) -> Option<Hash> {
        self.system_image_merkle()
    }
}

pub struct Mounts {
    pkg_resolver_data: DirOrProxy,
    pkg_resolver_config_data: DirOrProxy,
}

#[derive(Serialize)]
pub struct EnableDynamicConfig {
    pub enable_dynamic_configuration: bool,
}

#[derive(Serialize)]
pub struct PersistedReposConfig {
    pub persisted_repos_dir: String,
}

#[derive(Default)]
pub struct MountsBuilder {
    pkg_resolver_data: Option<DirOrProxy>,
    pkg_resolver_config_data: Option<DirOrProxy>,
    enable_dynamic_config: Option<EnableDynamicConfig>,
    static_repository: Option<RepositoryConfig>,
    dynamic_rewrite_rules: Option<RuleConfig>,
    dynamic_repositories: Option<RepositoryConfigs>,
    custom_config_data: Vec<(PathBuf, String)>,
    persisted_repos_config: Option<PersistedReposConfig>,
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
    pub fn enable_dynamic_config(mut self, config: EnableDynamicConfig) -> Self {
        self.enable_dynamic_config = Some(config);
        self
    }
    pub fn persisted_repos_config(mut self, config: PersistedReposConfig) -> Self {
        self.persisted_repos_config = Some(config);
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
    /// Injects a file with custom contents into /config/data. Panics if file already exists.
    pub fn custom_config_data(mut self, path: impl Into<PathBuf>, data: impl Into<String>) -> Self {
        self.custom_config_data.push((path.into(), data.into()));
        self
    }
    pub fn build(self) -> Mounts {
        let mounts = Mounts {
            pkg_resolver_data: self
                .pkg_resolver_data
                .unwrap_or_else(|| DirOrProxy::Dir(tempfile::tempdir().expect("/tmp to exist"))),
            pkg_resolver_config_data: self
                .pkg_resolver_config_data
                .unwrap_or_else(|| DirOrProxy::Dir(tempfile::tempdir().expect("/tmp to exist"))),
        };
        if let Some(config) = self.enable_dynamic_config {
            mounts.add_enable_dynamic_config(&config);
        }
        if let Some(config) = self.persisted_repos_config {
            mounts.add_persisted_repos_config(&config);
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
        for (path, data) in self.custom_config_data {
            mounts.add_custom_config_data(path, data);
        }
        mounts
    }
}

impl Mounts {
    fn add_enable_dynamic_config(&self, config: &EnableDynamicConfig) {
        if let DirOrProxy::Dir(ref d) = self.pkg_resolver_config_data {
            let mut f = BufWriter::new(File::create(d.path().join("config.json")).unwrap());
            serde_json::to_writer(&mut f, &config).unwrap();
            f.flush().unwrap();
        } else {
            panic!("not supported");
        }
    }

    fn add_persisted_repos_config(&self, config: &PersistedReposConfig) {
        if let DirOrProxy::Dir(ref d) = self.pkg_resolver_config_data {
            let mut f =
                BufWriter::new(File::create(d.path().join("persisted_repos_dir.json")).unwrap());
            serde_json::to_writer(&mut f, &config).unwrap();
            f.flush().unwrap();
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
            let mut f = BufWriter::new(
                File::create(static_repo_path.join(format!("{}.json", config.repo_url().host())))
                    .unwrap(),
            );
            serde_json::to_writer(&mut f, &RepositoryConfigs::Version1(vec![config])).unwrap();
            f.flush().unwrap();
        } else {
            panic!("not supported");
        }
    }

    fn add_dynamic_rewrite_rules(&self, rule_config: &RuleConfig) {
        if let DirOrProxy::Dir(ref d) = self.pkg_resolver_data {
            let mut f = BufWriter::new(File::create(d.path().join("rewrites.json")).unwrap());
            serde_json::to_writer(&mut f, rule_config).unwrap();
            f.flush().unwrap();
        } else {
            panic!("not supported");
        }
    }
    fn add_dynamic_repositories(&self, repo_configs: &RepositoryConfigs) {
        if let DirOrProxy::Dir(ref d) = self.pkg_resolver_data {
            let mut f = BufWriter::new(File::create(d.path().join("repositories.json")).unwrap());
            serde_json::to_writer(&mut f, repo_configs).unwrap();
            f.flush().unwrap();
        } else {
            panic!("not supported");
        }
    }

    fn add_custom_config_data(&self, path: impl AsRef<Path>, data: String) {
        if let DirOrProxy::Dir(ref d) = self.pkg_resolver_config_data {
            let path = d.path().join(path);
            assert!(!path.exists());
            std::fs::create_dir_all(path.parent().unwrap()).unwrap();
            std::fs::write(path, &data).unwrap();
        } else {
            panic!("not supported");
        }
    }
}

pub enum DirOrProxy {
    Dir(TempDir),
    Proxy(DirectoryProxy),
}

impl DirOrProxy {
    fn to_proxy(&self, rights: u32) -> DirectoryProxy {
        match &self {
            DirOrProxy::Dir(d) => {
                io_util::directory::open_in_namespace(d.path().to_str().unwrap(), rights).unwrap()
            }
            DirOrProxy::Proxy(p) => clone_directory_proxy(p, rights),
        }
    }
}

pub fn clone_directory_proxy(proxy: &DirectoryProxy, rights: u32) -> DirectoryProxy {
    let (client, server) = fidl::endpoints::create_endpoints().unwrap();
    proxy.clone(rights, server).unwrap();
    ClientEnd::<DirectoryMarker>::new(client.into_channel()).into_proxy().unwrap()
}

async fn pkgfs_with_system_image() -> PkgfsRamdisk {
    let system_image_package = SystemImageBuilder::new();
    let system_image_package = system_image_package.build().await;
    pkgfs_with_system_image_and_pkg(&system_image_package, None).await
}

pub async fn pkgfs_with_system_image_and_pkg(
    system_image_package: &Package,
    pkg: Option<&Package>,
) -> PkgfsRamdisk {
    let blobfs = BlobfsRamdisk::start().unwrap();
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    if let Some(pkg) = pkg {
        pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    }
    PkgfsRamdisk::builder()
        .blobfs(blobfs)
        .system_image_merkle(system_image_package.meta_far_merkle_root())
        .start()
        .unwrap()
}

pub enum ResolverVariant {
    DefaultArgs,
    AllowLocalMirror,
    ZeroTufMetadataTimeout,
    ShortTufMetadataTimeout,
    ZeroBlobNetworkHeaderTimeout,
    ZeroBlobNetworkBodyTimeout,
    ZeroBlobDownloadResumptionAttemptsLimit,
}

pub struct TestEnvBuilder<PkgFsFn, PkgFsFut, MountsFn>
where
    PkgFsFn: FnOnce() -> PkgFsFut,
    PkgFsFut: Future,
{
    pkgfs: PkgFsFn,
    mounts: MountsFn,
    tuf_repo_config_boot_arg: Option<String>,
    local_mirror_repo: Option<(Arc<Repository>, RepoUrl)>,
    resolver_variant: ResolverVariant,
}

impl
    TestEnvBuilder<
        fn() -> BoxFuture<'static, PkgfsRamdisk>,
        BoxFuture<'static, PkgfsRamdisk>,
        fn() -> Mounts,
    >
{
    pub fn new() -> Self {
        Self {
            pkgfs: || pkgfs_with_system_image().boxed(),
            // If it's not overriden, the default state of the mounts allows for dynamic configuration.
            // We do this because in the majority of tests, we'll want to use dynamic repos and rewrite rules.
            // Note: this means that we'll produce different envs from TestEnvBuilder::new().build().await
            // vs TestEnvBuilder::new().mounts(MountsBuilder::new().build()).build()
            mounts: || {
                MountsBuilder::new()
                    .enable_dynamic_config(EnableDynamicConfig {
                        enable_dynamic_configuration: true,
                    })
                    .build()
            },
            tuf_repo_config_boot_arg: None,
            local_mirror_repo: None,
            resolver_variant: ResolverVariant::DefaultArgs,
        }
    }
}

impl<PkgFsFn, PkgFsFut, MountsFn> TestEnvBuilder<PkgFsFn, PkgFsFut, MountsFn>
where
    PkgFsFn: FnOnce() -> PkgFsFut,
    PkgFsFut: Future,
    PkgFsFut::Output: PkgFs,
    MountsFn: FnOnce() -> Mounts,
{
    pub fn pkgfs<Pother>(
        self,
        pkgfs: Pother,
    ) -> TestEnvBuilder<impl FnOnce() -> future::Ready<Pother>, future::Ready<Pother>, MountsFn>
    where
        Pother: PkgFs + 'static,
    {
        TestEnvBuilder::<_, _, MountsFn> {
            pkgfs: || future::ready(pkgfs),
            mounts: self.mounts,
            tuf_repo_config_boot_arg: self.tuf_repo_config_boot_arg,
            local_mirror_repo: self.local_mirror_repo,
            resolver_variant: self.resolver_variant,
        }
    }
    pub fn mounts(
        self,
        mounts: Mounts,
    ) -> TestEnvBuilder<PkgFsFn, PkgFsFut, impl FnOnce() -> Mounts> {
        TestEnvBuilder::<PkgFsFn, _, _> {
            pkgfs: self.pkgfs,
            mounts: || mounts,
            tuf_repo_config_boot_arg: self.tuf_repo_config_boot_arg,
            local_mirror_repo: self.local_mirror_repo,
            resolver_variant: self.resolver_variant,
        }
    }
    pub fn tuf_repo_config_boot_arg(mut self, repo: String) -> Self {
        assert_eq!(self.tuf_repo_config_boot_arg, None);
        self.tuf_repo_config_boot_arg = Some(repo);
        self
    }

    pub fn local_mirror_repo(mut self, repo: &Arc<Repository>, hostname: RepoUrl) -> Self {
        self.local_mirror_repo = Some((repo.clone(), hostname));
        self
    }

    pub fn resolver_variant(mut self, variant: ResolverVariant) -> Self {
        self.resolver_variant = variant;
        self
    }

    pub async fn build(self) -> TestEnv<PkgFsFut::Output> {
        let pkgfs = (self.pkgfs)().await;
        let mounts = (self.mounts)();

        let local_child_svc_dir = vfs::pseudo_directory! {};

        let mut args = HashMap::new();
        args.insert("tuf_repo_config".to_string(), self.tuf_repo_config_boot_arg);
        let mut boot_arguments_service = MockBootArgumentsService::new(args);
        pkgfs.system_image_hash().map(|hash| boot_arguments_service.insert_pkgfs_boot_arg(hash));
        let boot_arguments_service = Arc::new(boot_arguments_service);
        local_child_svc_dir
            .add_entry(
                fidl_fuchsia_boot::ArgumentsMarker::PROTOCOL_NAME,
                vfs::service::host(move |stream| {
                    Arc::clone(&boot_arguments_service).handle_request_stream(stream)
                }),
            )
            .unwrap();

        let logger_factory = Arc::new(MockLoggerFactory::new());
        let logger_factory_clone = Arc::clone(&logger_factory);
        local_child_svc_dir
            .add_entry(
                fidl_fuchsia_cobalt::LoggerFactoryMarker::PROTOCOL_NAME,
                vfs::service::host(move |stream| {
                    Arc::clone(&logger_factory_clone).run_logger_factory(stream)
                }),
            )
            .unwrap();

        let local_child_out_dir = vfs::pseudo_directory! {
            "pkgfs" => vfs::remote::remote_dir(
                pkgfs.root_dir_handle().unwrap().into_proxy().unwrap()
            ),
            "blob" => vfs::remote::remote_dir(
                pkgfs.blobfs_root_dir_handle().expect("blob dir to open").into_proxy().unwrap()
            ),
            "data" => vfs::remote::remote_dir(
                mounts.pkg_resolver_data.to_proxy(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE)
            ),
            "config" => vfs::pseudo_directory! {
                "data" => vfs::remote::remote_dir(
                    mounts.pkg_resolver_config_data.to_proxy(OPEN_RIGHT_READABLE)
                ),
                "ssl" => vfs::remote::remote_dir(
                    io_util::directory::open_in_namespace(
                        "/pkg/data/ssl",
                        OPEN_RIGHT_READABLE
                    ).unwrap()
                ),
            },
            "svc" => local_child_svc_dir,
        };

        let local_mirror_dir = tempfile::tempdir().unwrap();
        if let Some((repo, url)) = self.local_mirror_repo {
            let proxy = io_util::directory::open_in_namespace(
                local_mirror_dir.path().to_str().unwrap(),
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            )
            .unwrap();
            let () = repo.copy_local_repository_to_dir(&proxy, &url).await;
            let () = local_child_out_dir
                .add_entry(
                    "usb",
                    vfs::pseudo_directory! {
                        "0" => vfs::pseudo_directory! {
                            "fuchsia_pkg" => vfs::remote::remote_dir(proxy),
                        },
                    },
                )
                .unwrap();
        }

        let local_child_out_dir = Mutex::new(Some(local_child_out_dir));

        let builder = RealmBuilder::new().await.unwrap();
        let pkg_cache = builder
            .add_child(
                "pkg_cache",
                "fuchsia-pkg://fuchsia.com/pkg-resolver-integration-tests#meta/pkg-cache.cm",
                ChildOptions::new(),
            )
            .await
            .unwrap();
        let system_update_committer = builder
            .add_child("system_update_committer", "fuchsia-pkg://fuchsia.com/pkg-resolver-integration-tests#meta/system-update-committer.cm", ChildOptions::new()).await.unwrap();
        let service_reflector = builder
            .add_local_child(
                "service_reflector",
                move |handles| {
                    let local_child_out_dir = local_child_out_dir
                        .lock()
                        .take()
                        .expect("mock component should only be launched once");
                    let scope = vfs::execution_scope::ExecutionScope::new();
                    let () = local_child_out_dir.open(
                        scope.clone(),
                        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_RIGHT_EXECUTABLE,
                        0,
                        vfs::path::Path::dot(),
                        handles.outgoing_dir.into_channel().into(),
                    );
                    async move { Ok(scope.wait().await) }.boxed()
                },
                ChildOptions::new(),
            )
            .await
            .unwrap();
        let local_mirror = builder
            .add_child(
                "local_mirror",
                "fuchsia-pkg://fuchsia.com/pkg-resolver-integration-tests#meta/pkg-local-mirror.cm",
                ChildOptions::new(),
            )
            .await
            .unwrap();
        let pkg_resolver_wrapper = builder
            .add_child("pkg_resolver_wrapper", "fuchsia-pkg://fuchsia.com/pkg-resolver-integration-tests#meta/pkg-resolver-wrapper.cm", ChildOptions::new()).await.unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&pkg_cache)
                    .to(&system_update_committer)
                    .to(&local_mirror)
                    .to(&pkg_resolver_wrapper),
            )
            .await
            .unwrap();

        // Make sure pkg_resolver has network access as required by the hyper client shard
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.net.name.Lookup"))
                    .capability(Capability::protocol_by_name("fuchsia.posix.socket.Provider"))
                    .from(Ref::parent())
                    .to(&pkg_resolver_wrapper),
            )
            .await
            .unwrap();

        // Fill out the rest of the `use` stanzas for pkg_resolver and pkg_cache
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.boot.Arguments"))
                    .capability(Capability::protocol_by_name("fuchsia.cobalt.LoggerFactory"))
                    .capability(Capability::protocol_by_name("fuchsia.tracing.provider.Registry"))
                    .from(&service_reflector)
                    .to(&pkg_cache)
                    .to(&pkg_resolver_wrapper),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.pkg.LocalMirror"))
                    .from(&local_mirror)
                    .to(&pkg_resolver_wrapper),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.pkg.PackageCache"))
                    .from(&pkg_cache)
                    .to(&pkg_resolver_wrapper)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();

        // Route the Realm protocol from the pkg_resolver_wrapper so that the test can control
        // starting and stopping pkg-resolver.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.component.Realm"))
                    .from(&pkg_resolver_wrapper)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();

        // Directory routes
        builder
            .add_route(
                Route::new()
                    .capability(
                        Capability::directory("pkgfs")
                            .path("/pkgfs")
                            .rights(fidl_fuchsia_io::RW_STAR_DIR),
                    )
                    .capability(
                        Capability::directory("blob")
                            .path("/blob")
                            .rights(fidl_fuchsia_io::RW_STAR_DIR),
                    )
                    .from(&service_reflector)
                    .to(&pkg_cache),
            )
            .await
            .unwrap();

        // route mock /usb to local_mirror
        builder
            .add_route(
                Route::new()
                    .capability(
                        Capability::directory("usb")
                            .path("/usb")
                            .rights(fidl_fuchsia_io::RW_STAR_DIR),
                    )
                    .from(&service_reflector)
                    .to(&local_mirror),
            )
            .await
            .unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(
                        Capability::directory("config-data")
                            .path("/config/data")
                            .rights(fidl_fuchsia_io::R_STAR_DIR),
                    )
                    .capability(
                        Capability::directory("root-ssl-certificates")
                            .path("/config/ssl")
                            .rights(fidl_fuchsia_io::R_STAR_DIR),
                    )
                    .from(&service_reflector)
                    .to(&pkg_resolver_wrapper),
            )
            .await
            .unwrap();

        // TODO(fxbug.dev/75658): Change to storage once convenient.
        builder
            .add_route(
                Route::new()
                    .capability(
                        Capability::directory("data")
                            .path("/data")
                            .rights(fidl_fuchsia_io::RW_STAR_DIR),
                    )
                    .from(&service_reflector)
                    .to(&pkg_resolver_wrapper),
            )
            .await
            .unwrap();

        let realm_instance = builder.build().await.unwrap();

        let realm = realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<RealmMarker>()
            .expect("connect to pkg_resolver_wrapper Realm");
        let pkg_resolver = start_pkg_resolver(realm, &self.resolver_variant).await;

        TestEnv {
            pkgfs,
            proxies: Proxies::from_instance(&pkg_resolver),
            apps: Apps { realm_instance, pkg_resolver: Some(pkg_resolver) },
            _mounts: mounts,
            mocks: Mocks { logger_factory },
            local_mirror_dir,
            resolver_variant: self.resolver_variant,
        }
    }
}

async fn start_pkg_resolver(realm: RealmProxy, variant: &ResolverVariant) -> ScopedInstance {
    // The only differences between these packages is additional arguments
    // to the binary declared in the program section of the .cml file.
    let url = match variant {
        ResolverVariant::DefaultArgs => "fuchsia-pkg://fuchsia.com/pkg-resolver-integration-tests#meta/pkg-resolver.cm",
        ResolverVariant::AllowLocalMirror => "fuchsia-pkg://fuchsia.com/pkg-resolver-integration-tests#meta/pkg-resolver-allow-local-mirror.cm",
        ResolverVariant::ZeroTufMetadataTimeout => "fuchsia-pkg://fuchsia.com/pkg-resolver-integration-tests#meta/pkg-resolver-zero-tuf-metadata-timeout.cm",
        ResolverVariant::ShortTufMetadataTimeout => "fuchsia-pkg://fuchsia.com/pkg-resolver-integration-tests#meta/pkg-resolver-short-tuf-metadata-timeout.cm",
        ResolverVariant::ZeroBlobNetworkHeaderTimeout => "fuchsia-pkg://fuchsia.com/pkg-resolver-integration-tests#meta/pkg-resolver-zero-blob-network-header-timeout.cm",
        ResolverVariant::ZeroBlobNetworkBodyTimeout => "fuchsia-pkg://fuchsia.com/pkg-resolver-integration-tests#meta/pkg-resolver-zero-blob-network-body-timeout.cm",
        ResolverVariant::ZeroBlobDownloadResumptionAttemptsLimit => "fuchsia-pkg://fuchsia.com/pkg-resolver-integration-tests#meta/pkg-resolver-zero-blob-download-resumption-attempts.cm",
    };
    ScopedInstanceFactory::new("pkg-resolver-coll")
        .with_realm_proxy(realm)
        .new_named_instance("pkg-resolver", url)
        .await
        .expect("failed to launch pkg-resolver")
}

pub struct Apps {
    pub realm_instance: RealmInstance,
    pub pkg_resolver: Option<ScopedInstance>,
}

pub struct Proxies {
    pub resolver_admin: PackageResolverAdminProxy,
    pub resolver: PackageResolverProxy,
    pub repo_manager: RepositoryManagerProxy,
    pub rewrite_engine: RewriteEngineProxy,
    pub font_resolver: FontResolverProxy,
}

impl Proxies {
    fn from_instance(instance: &ScopedInstance) -> Proxies {
        Proxies {
            resolver: instance
                .connect_to_protocol_at_exposed_dir::<PackageResolverMarker>()
                .expect("connect to package resolver"),
            resolver_admin: instance
                .connect_to_protocol_at_exposed_dir::<PackageResolverAdminMarker>()
                .expect("connect to package resolver admin"),
            repo_manager: instance
                .connect_to_protocol_at_exposed_dir::<RepositoryManagerMarker>()
                .expect("connect to repository manager"),
            rewrite_engine: instance
                .connect_to_protocol_at_exposed_dir::<RewriteEngineMarker>()
                .expect("connect to rewrite engine"),
            font_resolver: instance
                .connect_to_protocol_at_exposed_dir::<FontResolverMarker>()
                .expect("connect to font resolver"),
        }
    }
}

pub struct Mocks {
    pub logger_factory: Arc<MockLoggerFactory>,
}

pub struct TestEnv<P = PkgfsRamdisk> {
    pub pkgfs: P,
    pub apps: Apps,
    pub proxies: Proxies,
    pub _mounts: Mounts,
    pub mocks: Mocks,
    pub local_mirror_dir: TempDir,
    resolver_variant: ResolverVariant,
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
        self.pkgfs.stop().await.expect("pkgfs to stop gracefully");
    }
}

struct MockLogger {
    cobalt_events: Mutex<Vec<CobaltEvent>>,
}

impl MockLogger {
    fn new() -> Self {
        Self { cobalt_events: Mutex::new(vec![]) }
    }

    async fn run_logger(self: Arc<Self>, mut stream: fidl_fuchsia_cobalt::LoggerRequestStream) {
        while let Some(event) = stream.try_next().await.unwrap() {
            match event {
                fidl_fuchsia_cobalt::LoggerRequest::LogCobaltEvent { event, responder } => {
                    self.cobalt_events.lock().push(event);
                    let _ = responder.send(fidl_fuchsia_cobalt::Status::Ok);
                }
                _ => {
                    panic!("unhandled Logger method {:?}", event);
                }
            }
        }
    }
}

pub struct MockLoggerFactory {
    loggers: Mutex<Vec<Arc<MockLogger>>>,
}

impl MockLoggerFactory {
    fn new() -> Self {
        Self { loggers: Mutex::new(vec![]) }
    }

    async fn run_logger_factory(
        self: Arc<Self>,
        mut stream: fidl_fuchsia_cobalt::LoggerFactoryRequestStream,
    ) {
        while let Some(event) = stream.try_next().await.unwrap() {
            match event {
                fidl_fuchsia_cobalt::LoggerFactoryRequest::CreateLoggerFromProjectId {
                    project_id,
                    logger,
                    responder,
                } => {
                    assert_eq!(project_id, cobalt_sw_delivery_registry::PROJECT_ID);
                    let mock_logger = Arc::new(MockLogger::new());
                    self.loggers.lock().push(mock_logger.clone());
                    fasync::Task::spawn(mock_logger.run_logger(logger.into_stream().unwrap()))
                        .detach();
                    let _ = responder.send(fidl_fuchsia_cobalt::Status::Ok);
                }
                _ => {
                    panic!("unhandled LoggerFactory method: {:?}", event);
                }
            }
        }
    }

    pub async fn wait_for_at_least_n_events_with_metric_id(
        &self,
        n: usize,
        id: u32,
    ) -> Vec<CobaltEvent> {
        loop {
            let events: Vec<CobaltEvent> = self
                .loggers
                .lock()
                .iter()
                .flat_map(|logger| logger.cobalt_events.lock().clone().into_iter())
                .filter(|CobaltEvent { metric_id, .. }| *metric_id == id)
                .collect();
            if events.len() >= n {
                return events;
            }
            fasync::Timer::new(Duration::from_millis(10)).await;
        }
    }
}

impl<P: PkgFs> TestEnv<P> {
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
        let () = self.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();
    }

    pub async fn restart_pkg_resolver(&mut self) {
        let waiter = self.apps.pkg_resolver.as_mut().unwrap().take_destroy_waiter();
        drop(self.apps.pkg_resolver.take());
        waiter.await.expect("failed to destroy pkg-resolver");

        let realm = self
            .apps
            .realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<RealmMarker>()
            .expect("connect to pkg_resolver_wrapper Realm");
        self.apps.pkg_resolver = Some(start_pkg_resolver(realm, &self.resolver_variant).await);
        self.proxies = Proxies::from_instance(self.apps.pkg_resolver.as_ref().unwrap());

        self.wait_for_pkg_resolver_to_start().await;
    }

    pub async fn wait_for_pkg_resolver_to_start(&self) {
        self.proxies
            .rewrite_engine
            .test_apply("fuchsia-pkg://test.com/name")
            .await
            .expect("fidl call succeeds")
            .expect("test apply result is ok");
    }

    pub fn connect_to_resolver(&self) -> PackageResolverProxy {
        self.apps
            .pkg_resolver
            .as_ref()
            .unwrap()
            .connect_to_protocol_at_exposed_dir::<PackageResolverMarker>()
            .expect("connect to package resolver")
    }

    pub fn resolve_package(
        &self,
        url: &str,
    ) -> impl Future<Output = Result<DirectoryProxy, fidl_fuchsia_pkg::ResolveError>> {
        resolve_package(&self.proxies.resolver, url)
    }

    pub fn get_hash(&self, url: impl Into<String>) -> impl Future<Output = Result<BlobId, Status>> {
        let fut =
            self.proxies.resolver.get_hash(&mut fidl_fuchsia_pkg::PackageUrl { url: url.into() });
        async move { fut.await.unwrap().map(|blob_id| blob_id.into()).map_err(|i| Status::from_raw(i)) }
    }

    pub async fn open_cached_package(&self, hash: BlobId) -> Result<DirectoryProxy, zx::Status> {
        let cache_service = self
            .apps
            .realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<PackageCacheMarker>()
            .unwrap();
        let (proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
        let () = cache_service
            .open(&mut hash.into(), server_end)
            .await
            .unwrap()
            .map_err(zx::Status::from_raw)?;
        Ok(proxy)
    }

    pub async fn pkg_resolver_inspect_hierarchy(&self) -> DiagnosticsHierarchy {
        let nested_environment_label =
            format!("realm_builder\\:{}", self.apps.realm_instance.root.child_name());
        ArchiveReader::new()
            .add_selector(ComponentSelector::new(vec![
                nested_environment_label.to_string(),
                "pkg_resolver_wrapper".to_string(),
                "pkg-resolver-coll\\:pkg-resolver".to_string(),
            ]))
            .snapshot::<Inspect>()
            .await
            .expect("read inspect hierarchy")
            .into_iter()
            .next()
            .expect("one result")
            .payload
            .expect("payload is not none")
    }

    /// Wait until pkg-resolver inspect state satisfies `desired_state`.
    pub async fn wait_for_pkg_resolver_inspect_state(&self, desired_state: TreeAssertion<String>) {
        while desired_state.run(&self.pkg_resolver_inspect_hierarchy().await).is_err() {
            fasync::Timer::new(Duration::from_millis(10)).await;
        }
    }
    /// Wait until at least `expected_event_codes.len()` events of metric id `expected_metric_id`
    /// are received, then assert that the event codes of the received events correspond, in order,
    /// to the event codes in `expected_event_codes`.
    pub async fn assert_count_events(
        &self,
        expected_metric_id: u32,
        expected_event_codes: Vec<impl AsEventCodes>,
    ) {
        let actual_events = self
            .mocks
            .logger_factory
            .wait_for_at_least_n_events_with_metric_id(
                expected_event_codes.len(),
                expected_metric_id,
            )
            .await;
        assert_eq!(
            actual_events.len(),
            expected_event_codes.len(),
            "event count different than expected, actual_events: {:?}",
            actual_events
        );

        for (event, expected_codes) in actual_events
            .into_iter()
            .zip(expected_event_codes.into_iter().map(|c| c.as_event_codes()))
        {
            assert_matches!(
                event,
                CobaltEvent {
                    metric_id,
                    event_codes,
                    component: None,
                    payload: EventPayload::EventCount(CountEvent {
                        period_duration_micros: 0,
                        count: 1
                    }),
                } if metric_id == expected_metric_id && event_codes == expected_codes
            )
        }
    }
}

pub const EMPTY_REPO_PATH: &str = "/pkg/empty-repo";

// The following functions generate unique test package dummy content. Callers are recommended
// to pass in the name of the test case.
pub fn test_package_bin(s: &str) -> Vec<u8> {
    return format!("!/boot/bin/sh\n{}", s).as_bytes().to_owned();
}

pub fn test_package_cml(s: &str) -> Vec<u8> {
    return format!("{{program:{{runner:\"elf\",binary:\"bin/{}\"}}}}", s).as_bytes().to_owned();
}

pub fn extra_blob_contents(s: &str, i: u32) -> Vec<u8> {
    format!("contents of file {}-{}", s, i).as_bytes().to_owned()
}

pub async fn make_pkg_with_extra_blobs(s: &str, n: u32) -> Package {
    let mut pkg = PackageBuilder::new(s)
        .add_resource_at(format!("bin/{}", s), &test_package_bin(s)[..])
        .add_resource_at(format!("meta/{}.cml", s), &test_package_cml(s)[..]);
    for i in 0..n {
        pkg =
            pkg.add_resource_at(format!("data/{}-{}", s, i), extra_blob_contents(s, i).as_slice());
    }
    pkg.build().await.unwrap()
}

pub fn resolve_package(
    resolver: &PackageResolverProxy,
    url: &str,
) -> impl Future<Output = Result<DirectoryProxy, fidl_fuchsia_pkg::ResolveError>> {
    let (package, package_server_end) = fidl::endpoints::create_proxy().unwrap();
    let response_fut = resolver.resolve(url, package_server_end);
    async move {
        let () = response_fut.await.unwrap()?;
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
