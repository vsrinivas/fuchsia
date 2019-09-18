// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    failure::Error,
    fidl::{
        encoding::OutOfLine,
        endpoints::{ClientEnd, ServerEnd},
    },
    fidl_fuchsia_amber::ControlMarker as AmberMarker,
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, FileControlHandle, FileMarker, FileObject, FileRequest,
        FileRequestStream, NodeInfo, NodeMarker, DIRENT_TYPE_FILE, INO_UNKNOWN,
    },
    fidl_fuchsia_pkg::{
        ExperimentToggle as Experiment, PackageCacheMarker, PackageResolverAdminMarker,
        PackageResolverAdminProxy, PackageResolverMarker, PackageResolverProxy,
        RepositoryManagerMarker, RepositoryManagerProxy, UpdatePolicy,
    },
    fidl_fuchsia_pkg_ext::MirrorConfigBuilder,
    fidl_fuchsia_sys::LauncherProxy,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_merkle::{Hash, MerkleTree},
    fuchsia_pkg_testing::{pkgfs::TestPkgFs, Package, PackageBuilder, RepositoryBuilder},
    fuchsia_vfs_pseudo_fs::{
        directory::entry::DirectoryEntry, directory::entry::EntryInfo, pseudo_directory,
    },
    fuchsia_zircon::Status,
    futures::{future::FusedFuture, prelude::*},
    matches::assert_matches,
    std::{
        fs::File,
        io::{self, Read},
        sync::{atomic::AtomicU64, Arc},
    },
};

trait PkgFs {
    fn root_dir_client_end(&self) -> Result<ClientEnd<DirectoryMarker>, Error>;
}

impl PkgFs for TestPkgFs {
    fn root_dir_client_end(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        TestPkgFs::root_dir_client_end(self)
    }
}

struct Proxies {
    resolver_admin: PackageResolverAdminProxy,
    resolver: PackageResolverProxy,
    repo_manager: RepositoryManagerProxy,
}

struct TestEnv<P = TestPkgFs> {
    _amber: App,
    _pkg_cache: App,
    _pkg_resolver: App,
    pkgfs: P,
    env: NestedEnvironment,
    proxies: Proxies,
}

impl TestEnv<TestPkgFs> {
    fn new() -> Self {
        Self::new_with_pkg_fs(TestPkgFs::start(None).expect("pkgfs to start"))
    }
}

impl TestEnv<TestPkgFs> {
    fn add_file_with_merkle_to_blobfs(&self, mut file: File, merkle: &Hash) {
        let mut blob = self
            .pkgfs
            .blobfs()
            .as_dir()
            .expect("blobfs has root dir")
            .write_file(merkle.to_string(), 0)
            .expect("create file in blobfs");
        blob.set_len(file.metadata().expect("file has metadata").len()).expect("set_len");
        io::copy(&mut file, &mut blob).expect("copy file to blobfs");
    }

    fn add_slice_to_blobfs(&self, slice: &[u8]) {
        let merkle = MerkleTree::from_reader(slice).expect("merkle slice").root().to_string();
        let mut blob = self
            .pkgfs
            .blobfs()
            .as_dir()
            .expect("blobfs has root dir")
            .write_file(merkle, 0)
            .expect("create file in blobfs");
        blob.set_len(slice.len() as u64).expect("set_len");
        io::copy(&mut &slice[..], &mut blob).expect("copy from slice to blob");
    }

    fn add_file_to_pkgfs_at_path(&self, mut file: File, path: impl openat::AsPath) {
        let mut blob = self
            .pkgfs
            .root_dir()
            .expect("pkgfs root_dir")
            .new_file(path, 0)
            .expect("create file in pkgfs");
        blob.set_len(file.metadata().expect("file has metadata").len()).expect("set_len");
        io::copy(&mut file, &mut blob).expect("copy file to pkgfs");
    }

    fn partially_add_file_to_pkgfs_at_path(&self, mut file: File, path: impl openat::AsPath) {
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

    fn partially_add_slice_to_pkgfs_at_path(&self, slice: &[u8], path: impl openat::AsPath) {
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
}

impl<P: PkgFs> TestEnv<P> {
    fn new_with_pkg_fs(pkgfs: P) -> Self {
        let mut amber =
            AppBuilder::new("fuchsia-pkg://fuchsia.com/pkg-resolver-tests#meta/amber.cmx")
                .add_handle_to_namespace(
                    "/pkgfs".to_owned(),
                    pkgfs.root_dir_client_end().expect("pkgfs dir to open").into(),
                );

        let mut pkg_cache = AppBuilder::new(
            "fuchsia-pkg://fuchsia.com/pkg-resolver-tests#meta/pkg_cache.cmx".to_owned(),
        )
        .add_handle_to_namespace(
            "/pkgfs".to_owned(),
            pkgfs.root_dir_client_end().expect("pkgfs dir to open").into(),
        );

        let mut pkg_resolver = AppBuilder::new(
            "fuchsia-pkg://fuchsia.com/pkg-resolver-tests#meta/pkg_resolver.cmx".to_owned(),
        )
        .add_handle_to_namespace(
            "/pkgfs".to_owned(),
            pkgfs.root_dir_client_end().expect("pkgfs dir to open").into(),
        );

        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_net::NameLookupMarker, _>()
            .add_proxy_service::<fidl_fuchsia_posix_socket::ProviderMarker, _>()
            .add_proxy_service_to::<AmberMarker, _>(amber.directory_request().unwrap().clone())
            .add_proxy_service_to::<PackageCacheMarker, _>(
                pkg_cache.directory_request().unwrap().clone(),
            )
            .add_proxy_service_to::<RepositoryManagerMarker, _>(
                pkg_resolver.directory_request().unwrap().clone(),
            )
            .add_proxy_service_to::<PackageResolverAdminMarker, _>(
                pkg_resolver.directory_request().unwrap().clone(),
            )
            .add_proxy_service_to::<PackageResolverMarker, _>(
                pkg_resolver.directory_request().unwrap().clone(),
            );

        let env = fs
            .create_salted_nested_environment("pkg-resolver-env")
            .expect("nested environment to create successfully");
        fasync::spawn(fs.collect());

        let amber = amber.spawn(env.launcher()).expect("amber to launch");
        let pkg_cache = pkg_cache.spawn(env.launcher()).expect("package cache to launch");
        let pkg_resolver = pkg_resolver.spawn(env.launcher()).expect("package resolver to launch");

        let resolver_proxy =
            env.connect_to_service::<PackageResolverMarker>().expect("connect to package resolver");
        let resolver_admin_proxy = env
            .connect_to_service::<PackageResolverAdminMarker>()
            .expect("connect to package resolver admin");
        let repo_manager_proxy = env
            .connect_to_service::<RepositoryManagerMarker>()
            .expect("connect to repository manager");

        Self {
            _amber: amber,
            _pkg_cache: pkg_cache,
            _pkg_resolver: pkg_resolver,
            env,
            pkgfs,
            proxies: Proxies {
                resolver: resolver_proxy,
                resolver_admin: resolver_admin_proxy,
                repo_manager: repo_manager_proxy,
            },
        }
    }

    fn launcher(&self) -> &LauncherProxy {
        self.env.launcher()
    }

    async fn set_experiment_state(&self, experiment: Experiment, state: bool) {
        self.proxies
            .resolver_admin
            .set_experiment_state(experiment, state)
            .await
            .expect("experiment state to toggle");
    }

    async fn resolve_package(&self, url: &str) -> Result<DirectoryProxy, Status> {
        let (package, package_server_end) = fidl::endpoints::create_proxy().unwrap();
        let selectors: Vec<&str> = vec![];
        let status = self
            .proxies
            .resolver
            .resolve(
                url,
                &mut selectors.into_iter(),
                &mut UpdatePolicy { fetch_if_absent: true, allow_old_versions: false },
                package_server_end,
            )
            .await
            .expect("package resolve fidl call");
        Status::ok(status)?;
        Ok(package)
    }
}

const ROLLDICE_BIN: &'static [u8] = b"#!/boot/bin/sh\necho 4\n";
const ROLLDICE_CMX: &'static [u8] = br#"{"program":{"binary":"bin/rolldice"}}"#;

fn extra_blob_contents(i: u32) -> Vec<u8> {
    format!("contents of file {}", i).as_bytes().to_owned()
}

async fn make_rolldice_pkg_with_extra_blobs(n: u32) -> Result<Package, Error> {
    let mut pkg = PackageBuilder::new("rolldice")
        .add_resource_at("bin/rolldice", ROLLDICE_BIN)?
        .add_resource_at("meta/rolldice.cmx", ROLLDICE_CMX)?;
    for i in 0..n {
        pkg = pkg.add_resource_at(format!("data/file{}", i), extra_blob_contents(i).as_slice())?;
    }
    pkg.build().await
}

#[fasync::run_singlethreaded(test)]
async fn test_package_resolution() -> Result<(), Error> {
    let env = TestEnv::new();

    let pkg = PackageBuilder::new("rolldice")
        .add_resource_at("bin/rolldice", ROLLDICE_BIN)?
        .add_resource_at("meta/rolldice.cmx", ROLLDICE_CMX)?
        .add_resource_at("data/duplicate_a", "same contents".as_bytes())?
        .add_resource_at("data/duplicate_b", "same contents".as_bytes())?
        .build()
        .await?;
    let repo = RepositoryBuilder::new().add_package(&pkg).build().await?;
    let served_repository = repo.serve(env.launcher()).await?;

    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);

    env.proxies.repo_manager.add(repo_config.into()).await?;

    let package = env
        .resolve_package("fuchsia-pkg://test/rolldice")
        .await
        .expect("package to resolve without error");

    // Verify the served package directory contains the exact expected contents.
    pkg.verify_contents(&package).await.expect("correct package contents");

    // All blobs in the repository should now be present in blobfs.
    assert_eq!(env.pkgfs.blobfs().list_blobs().unwrap(), repo.list_blobs().unwrap());

    Ok(())
}

async fn verify_separate_blobs_url(download_blob: bool) -> Result<(), Error> {
    let env = TestEnv::new();
    let pkg = make_rolldice_pkg_with_extra_blobs(3).await?;
    let repo = RepositoryBuilder::new().add_package(&pkg).build().await?;
    let served_repository = repo.serve(env.launcher()).await?;

    // Rename the blobs directory so the blobs can't be found in the usual place.
    // Both amber and the package resolver currently require Content-Length headers when
    // downloading content blobs. "pm serve" will gzip compress paths that aren't prefixed with
    // "/blobs", which removes the Content-Length header. To ensure "pm serve" does not compress
    // the blobs stored at this alternate path, its name must start with "blobs".
    let repo_root = repo.path();
    std::fs::rename(repo_root.join("blobs"), repo_root.join("blobsbolb"))?;

    // Configure the repo manager with different TUF and blobs URLs.
    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let mut repo_config = served_repository.make_repo_config(repo_url);
    let mirror = &repo_config.mirrors()[0];
    let mirror = MirrorConfigBuilder::new(mirror.mirror_url())
        .subscribe(mirror.subscribe())
        .blob_mirror_url(format!("{}/blobsbolb", mirror.mirror_url()))
        .build();
    repo_config.insert_mirror(mirror).unwrap();
    env.proxies.repo_manager.add(repo_config.into()).await?;

    // Optionally use the new install flow.
    if download_blob {
        env.set_experiment_state(Experiment::DownloadBlob, true).await;
    }

    // Verify package installation from the split repo succeeds.
    let package = env
        .resolve_package("fuchsia-pkg://test/rolldice")
        .await
        .expect("package to resolve without error");
    pkg.verify_contents(&package).await.expect("correct package contents");
    std::fs::rename(repo_root.join("blobsbolb"), repo_root.join("blobs"))?;
    assert_eq!(env.pkgfs.blobfs().list_blobs().unwrap(), repo.list_blobs().unwrap());

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_separate_blobs_url() -> Result<(), Error> {
    verify_separate_blobs_url(false).await
}

#[fasync::run_singlethreaded(test)]
async fn test_download_blob_separate_blobs_url() -> Result<(), Error> {
    verify_separate_blobs_url(true).await
}

async fn verify_download_blob_resolve_with_altered_env(
    pkg: Package,
    alter_env: impl FnOnce(&TestEnv, &Package),
) -> Result<(), Error> {
    let env = TestEnv::new();

    let repo = RepositoryBuilder::new().add_package(&pkg).build().await?;
    let served_repository = repo.serve(env.launcher()).await?;

    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);

    env.proxies.repo_manager.add(repo_config.into()).await?;

    env.set_experiment_state(Experiment::DownloadBlob, true).await;

    alter_env(&env, &pkg);

    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());
    let package_dir = env.resolve_package(&pkg_url).await.expect("package to resolve");

    pkg.verify_contents(&package_dir).await.expect("correct package contents");
    assert_eq!(env.pkgfs.blobfs().list_blobs().unwrap(), repo.list_blobs().unwrap());

    Ok(())
}

fn verify_download_blob_resolve(pkg: Package) -> impl Future<Output = Result<(), Error>> {
    verify_download_blob_resolve_with_altered_env(pkg, |_, _| {})
}

#[fasync::run_singlethreaded(test)]
async fn test_download_blob_meta_far_only() -> Result<(), Error> {
    verify_download_blob_resolve(PackageBuilder::new("uniblob").build().await?).await
}

#[fasync::run_singlethreaded(test)]
async fn test_download_blob_meta_far_and_empty_blob() -> Result<(), Error> {
    verify_download_blob_resolve(
        PackageBuilder::new("emptyblob")
            .add_resource_at("data/empty", "".as_bytes())?
            .build()
            .await?,
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_download_blob_experiment_large_blobs() -> Result<(), Error> {
    verify_download_blob_resolve(
        PackageBuilder::new("numbers")
            .add_resource_at("bin/numbers", ROLLDICE_BIN)?
            .add_resource_at("data/ones", io::repeat(1).take(1 * 1024 * 1024))?
            .add_resource_at("data/twos", io::repeat(2).take(2 * 1024 * 1024))?
            .add_resource_at("data/threes", io::repeat(3).take(3 * 1024 * 1024))?
            .build()
            .await?,
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_download_blob_experiment_many_blobs() -> Result<(), Error> {
    verify_download_blob_resolve(make_rolldice_pkg_with_extra_blobs(200).await?).await
}

#[fasync::run_singlethreaded(test)]
async fn test_download_blob_experiment_identity() -> Result<(), Error> {
    verify_download_blob_resolve(Package::identity().await?).await
}

#[fasync::run_singlethreaded(test)]
async fn test_download_blob_experiment_identity_hyper() -> Result<(), Error> {
    let env = TestEnv::new();

    let pkg = Package::identity().await?;
    let repo = Arc::new(RepositoryBuilder::new().add_package(&pkg).build().await?);
    let served_repository = repo.build_server().start()?;
    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);

    env.proxies.repo_manager.add(repo_config.into()).await?;

    env.set_experiment_state(Experiment::DownloadBlob, true).await;

    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());
    let package_dir = env.resolve_package(&pkg_url).await.expect("package to resolve");

    pkg.verify_contents(&package_dir).await.expect("correct package contents");

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_download_blob_experiment_uses_cached_package() -> Result<(), Error> {
    let env = TestEnv::new();

    let pkg = PackageBuilder::new("resolve-twice")
        .add_resource_at("data/foo", "bar".as_bytes())?
        .build()
        .await?;
    let repo = RepositoryBuilder::new().add_package(&pkg).build().await?;
    let served_repository = repo.serve(env.launcher()).await?;

    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);

    env.set_experiment_state(Experiment::DownloadBlob, true).await;

    // the package can't be resolved before the repository is configured.
    assert_matches!(
        env.resolve_package("fuchsia-pkg://test/resolve-twice").await,
        Err(Status::NOT_FOUND)
    );

    env.proxies.repo_manager.add(repo_config.into()).await?;

    // package resolves as expected.
    let package_dir =
        env.resolve_package("fuchsia-pkg://test/resolve-twice").await.expect("package to resolve");
    pkg.verify_contents(&package_dir).await.expect("correct package contents");

    // if no mirrors are accessible, the cached package is returned.
    served_repository.stop().await;
    let package_dir =
        env.resolve_package("fuchsia-pkg://test/resolve-twice").await.expect("package to resolve");
    pkg.verify_contents(&package_dir).await.expect("correct package contents");

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_meta_far_installed_blobs_not_installed() -> Result<(), Error> {
    verify_download_blob_resolve_with_altered_env(
        make_rolldice_pkg_with_extra_blobs(3).await?,
        |env, pkg| {
            env.add_file_to_pkgfs_at_path(
                pkg.meta_far().expect("package has meta.far"),
                format!("install/pkg/{}", pkg.meta_far_merkle_root().to_string()),
            )
        },
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_meta_far_partially_installed() -> Result<(), Error> {
    verify_download_blob_resolve_with_altered_env(
        make_rolldice_pkg_with_extra_blobs(3).await?,
        |env, pkg| {
            env.partially_add_file_to_pkgfs_at_path(
                pkg.meta_far().expect("package has meta.far"),
                format!("install/pkg/{}", pkg.meta_far_merkle_root().to_string()),
            )
        },
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_meta_far_already_in_blobfs() -> Result<(), Error> {
    verify_download_blob_resolve_with_altered_env(
        make_rolldice_pkg_with_extra_blobs(3).await?,
        |env, pkg| {
            env.add_file_with_merkle_to_blobfs(
                pkg.meta_far().expect("package has meta.far"),
                pkg.meta_far_merkle_root(),
            )
        },
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_all_blobs_already_in_blobfs() -> Result<(), Error> {
    verify_download_blob_resolve_with_altered_env(
        make_rolldice_pkg_with_extra_blobs(3).await?,
        |env, pkg| {
            env.add_file_with_merkle_to_blobfs(
                pkg.meta_far().expect("package has meta.far"),
                pkg.meta_far_merkle_root(),
            );
            env.add_slice_to_blobfs(ROLLDICE_BIN);
            for i in 0..3 {
                env.add_slice_to_blobfs(extra_blob_contents(i).as_slice());
            }
        },
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_meta_far_installed_one_blob_in_blobfs() -> Result<(), Error> {
    verify_download_blob_resolve_with_altered_env(
        make_rolldice_pkg_with_extra_blobs(3).await?,
        |env, pkg| {
            env.add_file_to_pkgfs_at_path(
                pkg.meta_far().expect("package has meta.far"),
                format!("install/pkg/{}", pkg.meta_far_merkle_root().to_string()),
            );
            env.add_slice_to_blobfs(ROLLDICE_BIN);
        },
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_meta_far_installed_one_blob_partially_installed() -> Result<(), Error> {
    verify_download_blob_resolve_with_altered_env(
        make_rolldice_pkg_with_extra_blobs(3).await?,
        |env, pkg| {
            env.add_file_to_pkgfs_at_path(
                pkg.meta_far().expect("package has meta.far"),
                format!("install/pkg/{}", pkg.meta_far_merkle_root().to_string()),
            );
            env.partially_add_slice_to_pkgfs_at_path(
                ROLLDICE_BIN,
                format!(
                    "install/blob/{}",
                    MerkleTree::from_reader(ROLLDICE_BIN).expect("merkle slice").root().to_string()
                ),
            );
        },
    )
    .await
}

struct MockPkgFs {
    root_dir_proxy: DirectoryProxy,
}

impl MockPkgFs {
    fn new(mut directory_entry: impl DirectoryEntry + 'static) -> Self {
        let (client, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_io::NodeMarker>().expect("create_proxy");
        directory_entry.open(
            fidl_fuchsia_io::OPEN_RIGHT_READABLE
                | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE
                | fidl_fuchsia_io::OPEN_FLAG_DIRECTORY,
            fidl_fuchsia_io::MODE_TYPE_DIRECTORY,
            &mut std::iter::empty(),
            server,
        );
        fasync::spawn(async move {
            directory_entry.await;
        });
        Self {
            root_dir_proxy: DirectoryProxy::new(client.into_channel().expect("proxy to channel")),
        }
    }
}

impl PkgFs for MockPkgFs {
    fn root_dir_client_end(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        let (client, server) = fidl::endpoints::create_endpoints::<fidl_fuchsia_io::NodeMarker>()?;
        self.root_dir_proxy.clone(fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS, server)?;
        Ok(client.into_channel().into())
    }
}

#[derive(Clone)]
struct FakeFile<StreamHandler> {
    stream_handler: StreamHandler,
    call_count: Arc<AtomicU64>,
}

impl<StreamHandler> FakeFile<StreamHandler> {
    fn new_and_call_count(stream_handler: StreamHandler) -> (Self, Arc<AtomicU64>) {
        let call_count = Arc::new(AtomicU64::new(0));
        (Self { stream_handler, call_count: call_count.clone() }, call_count)
    }
}

// fuchsia_vfs_pseudo_fs's DirectoryEntry trait extends Future and FusedFuture
// so that FIDL connections established through DirectoryEntry::open can be
// handled without spawning. This is more complicated than just spawning and we
// don't need the advantages provided, so FakeFile has dummy impls.
impl<StreamHandler> Future for FakeFile<StreamHandler> {
    type Output = void::Void;
    fn poll(
        self: std::pin::Pin<&mut Self>,
        _cx: &mut futures::task::Context<'_>,
    ) -> fuchsia_async::futures::Poll<Self::Output> {
        fuchsia_async::futures::Poll::Pending
    }
}

impl<StreamHandler> FusedFuture for FakeFile<StreamHandler> {
    fn is_terminated(&self) -> bool {
        false
    }
}

impl<StreamHandler, F> DirectoryEntry for FakeFile<StreamHandler>
where
    F: Future<Output = ()> + Send,
    StreamHandler: Fn(Arc<AtomicU64>, FileRequestStream, FileControlHandle) -> F
        + Unpin
        + Send
        + Clone
        + 'static,
{
    fn open(
        &mut self,
        _flags: u32,
        _mode: u32,
        path: &mut dyn Iterator<Item = &str>,
        server_end: ServerEnd<NodeMarker>,
    ) {
        assert_eq!(path.collect::<Vec<&str>>(), Vec::<&str>::new());
        let server_end = ServerEnd::<FileMarker>::new(server_end.into_channel());
        let (stream, ch) =
            server_end.into_stream_and_control_handle().expect("split file server end");
        let handler = Clone::clone(self);
        fasync::spawn(async move {
            (handler.stream_handler)(handler.call_count, stream, ch).await;
        });
    }
    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)
    }
}

async fn handle_file_stream_fail_on_open(
    call_count: Arc<AtomicU64>,
    mut stream: FileRequestStream,
    ch: FileControlHandle,
) {
    ch.send_on_open_(Status::NO_MEMORY.into_raw(), None).expect("send on open");
    call_count.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
    while let Some(req) = stream.next().await {
        handle_file_req_panic(req.expect("file request unpack")).await;
    }
}

async fn handle_file_stream_fail_truncate(
    call_count: Arc<AtomicU64>,
    mut stream: FileRequestStream,
    ch: FileControlHandle,
) {
    ch.send_on_open_(
        Status::OK.into_raw(),
        Some(OutOfLine(&mut NodeInfo::File(FileObject { event: None }))),
    )
    .expect("send on open");
    while let Some(req) = stream.next().await {
        handle_file_req_fail_truncate(call_count.clone(), req.expect("file request unpack")).await;
    }
}

async fn handle_file_stream_fail_write(
    call_count: Arc<AtomicU64>,
    mut stream: FileRequestStream,
    ch: FileControlHandle,
) {
    ch.send_on_open_(
        Status::OK.into_raw(),
        Some(OutOfLine(&mut NodeInfo::File(FileObject { event: None }))),
    )
    .expect("send on open");
    while let Some(req) = stream.next().await {
        handle_file_req_fail_write(call_count.clone(), req.expect("file request unpack")).await;
    }
}

async fn handle_file_stream_success(
    _call_count: Arc<AtomicU64>,
    mut stream: FileRequestStream,
    ch: FileControlHandle,
) {
    ch.send_on_open_(
        Status::OK.into_raw(),
        Some(OutOfLine(&mut NodeInfo::File(FileObject { event: None }))),
    )
    .expect("send on open");
    while let Some(req) = stream.next().await {
        handle_file_req_success(req.expect("file request unpack")).await;
    }
}

async fn handle_file_req_panic(_req: FileRequest) {
    panic!("should not be called");
}

async fn handle_file_req_fail_truncate(call_count: Arc<AtomicU64>, req: FileRequest) {
    match req {
        FileRequest::Truncate { length: _length, responder } => {
            call_count.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
            responder.send(Status::NO_MEMORY.into_raw()).expect("send truncate response");
        }
        req => panic!("should only receive truncate requests: {:?}", req),
    }
}

async fn handle_file_req_fail_write(call_count: Arc<AtomicU64>, req: FileRequest) {
    match req {
        // PkgFs receives truncate before write, as it's writing through to BlobFs
        FileRequest::Truncate { length: _length, responder } => {
            responder.send(Status::OK.into_raw()).expect("send truncate response");
        }
        FileRequest::Write { data: _data, responder } => {
            call_count.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
            responder.send(Status::NO_MEMORY.into_raw(), 0).expect("send write response");
        }
        req => panic!("should only receive write and truncate requests: {:?}", req),
    }
}

async fn handle_file_req_success(req: FileRequest) {
    match req {
        FileRequest::Truncate { length: _length, responder } => {
            responder.send(Status::OK.into_raw()).expect("send truncate response");
        }
        FileRequest::Write { data, responder } => {
            responder.send(Status::OK.into_raw(), data.len() as u64).expect("send write response");
        }
        req => panic!("should only receive write and truncate requests: {:?}", req),
    }
}

struct PkgFsDirectoryBuilder {
    install_pkg: Option<(String, Box<dyn DirectoryEntry + 'static>)>,
    install_blob: Option<(String, Box<dyn DirectoryEntry + 'static>)>,
    needs_packages: Option<(String, String)>,
}

impl PkgFsDirectoryBuilder {
    fn new() -> Self {
        Self { install_pkg: None, install_blob: None, needs_packages: None }
    }
    fn install_pkg(
        mut self,
        merkle: impl Into<String>,
        entry: impl DirectoryEntry + 'static,
    ) -> Self {
        self.install_pkg = Some((merkle.into(), Box::new(entry)));
        self
    }
    fn install_blob(mut self, blob: String, entry: impl DirectoryEntry + 'static) -> Self {
        self.install_blob = Some((blob, Box::new(entry)));
        self
    }
    fn needs_packages(mut self, pkg: String, blob: String) -> Self {
        self.needs_packages = Some((pkg, blob));
        self
    }
    fn build(self) -> fuchsia_vfs_pseudo_fs::directory::simple::Simple<'static> {
        let mut install_pkg = pseudo_directory! {};
        if let Some((merkle, entry)) = self.install_pkg {
            install_pkg.add_boxed_entry(merkle.as_str(), entry).map_err(|_| ()).expect("add_entry");
        }
        let mut install_blob = pseudo_directory! {};
        if let Some((merkle, entry)) = self.install_blob {
            install_blob
                .add_boxed_entry(merkle.as_str(), entry)
                .map_err(|_| ())
                .expect("add_entry");
        }
        let mut needs_packages = pseudo_directory! {};
        if let Some((pkg, blob)) = self.needs_packages {
            let mut pkg_dir = pseudo_directory! {};
            pkg_dir
                .add_entry(&blob, fuchsia_vfs_pseudo_fs::file::simple::read_only(|| Ok(vec![])))
                .map_err(|_| ())
                .expect("add_entry");
            needs_packages.add_entry(&pkg, pkg_dir).map_err(|_| ()).expect("add_entry");
        }
        pseudo_directory! {
            "versions" => pseudo_directory! {},
            "packages" => pseudo_directory! {},
            "ctl" => pseudo_directory! {},
            "install" => pseudo_directory! {
                "pkg" => install_pkg,
                "blob" => install_blob,
            },
            "needs" => pseudo_directory! {
                "blobs" => pseudo_directory! {},
                "packages" => needs_packages,
            },
        }
    }
}

async fn make_pkg_for_mock_pkgfs_tests() -> Result<(Package, String, String), Error> {
    let pkg = make_rolldice_pkg_with_extra_blobs(1).await?;
    let pkg_merkle = pkg.meta_far_merkle_root().to_string();
    let blob_merkle = MerkleTree::from_reader(extra_blob_contents(0).as_slice())
        .expect("merkle slice")
        .root()
        .to_string();
    Ok((pkg, pkg_merkle, blob_merkle))
}

async fn make_mock_pkgfs_with_failing_install_pkg<StreamHandler, F>(
    file_request_stream_handler: StreamHandler,
) -> Result<(MockPkgFs, Package, Arc<AtomicU64>), Error>
where
    F: Future<Output = ()> + Send,
    StreamHandler: Fn(Arc<AtomicU64>, FileRequestStream, FileControlHandle) -> F
        + Unpin
        + Send
        + Clone
        + 'static,
{
    let (pkg, pkg_merkle, _) = make_pkg_for_mock_pkgfs_tests().await?;
    let (failing_file, call_count) = FakeFile::new_and_call_count(file_request_stream_handler);
    let d = PkgFsDirectoryBuilder::new().install_pkg(pkg_merkle, failing_file).build();
    Ok((MockPkgFs::new(d), pkg, call_count))
}

async fn make_mock_pkgfs_with_failing_install_blob<StreamHandler, F>(
    file_request_stream_handler: StreamHandler,
) -> Result<(MockPkgFs, Package, Arc<AtomicU64>), Error>
where
    F: Future<Output = ()> + Send,
    StreamHandler: Fn(Arc<AtomicU64>, FileRequestStream, FileControlHandle) -> F
        + Unpin
        + Send
        + Clone
        + 'static,
{
    let (pkg, pkg_merkle, blob_merkle) = make_pkg_for_mock_pkgfs_tests().await?;
    let (success_file, _) = FakeFile::new_and_call_count(handle_file_stream_success);
    let (failing_file, call_count) = FakeFile::new_and_call_count(file_request_stream_handler);
    let d = PkgFsDirectoryBuilder::new()
        .install_pkg(pkg_merkle.clone(), success_file)
        .install_blob(blob_merkle.clone(), failing_file)
        .needs_packages(pkg_merkle, blob_merkle)
        .build();
    Ok((MockPkgFs::new(d), pkg, call_count))
}

async fn assert_resolve_package_with_failing_pkgfs_fails(
    pkgfs: MockPkgFs,
    pkg: Package,
    failing_file_call_count: Arc<AtomicU64>,
) -> Result<(), Error> {
    let env = TestEnv::new_with_pkg_fs(pkgfs);
    let repo = RepositoryBuilder::new().add_package(&pkg).build().await?;
    let served_repository = repo.serve(env.launcher()).await?;
    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);
    env.proxies.repo_manager.add(repo_config.into()).await?;
    env.set_experiment_state(Experiment::DownloadBlob, true).await;

    let res = env.resolve_package("fuchsia-pkg://test/rolldice").await;

    assert_matches!(res, Err(Status::IO));
    assert_eq!(failing_file_call_count.load(std::sync::atomic::Ordering::SeqCst), 1);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_fails_on_open_far_in_install_pkg() -> Result<(), Error> {
    let (pkgfs, pkg, failing_file_call_count) =
        make_mock_pkgfs_with_failing_install_pkg(handle_file_stream_fail_on_open).await?;

    assert_resolve_package_with_failing_pkgfs_fails(pkgfs, pkg, failing_file_call_count).await
}

#[fasync::run_singlethreaded(test)]
async fn test_fails_truncate_far_in_install_pkg() -> Result<(), Error> {
    let (pkgfs, pkg, failing_file_call_count) =
        make_mock_pkgfs_with_failing_install_pkg(handle_file_stream_fail_truncate).await?;

    assert_resolve_package_with_failing_pkgfs_fails(pkgfs, pkg, failing_file_call_count).await
}

#[fasync::run_singlethreaded(test)]
async fn test_fails_write_far_in_install_pkg() -> Result<(), Error> {
    let (pkgfs, pkg, failing_file_call_count) =
        make_mock_pkgfs_with_failing_install_pkg(handle_file_stream_fail_write).await?;

    assert_resolve_package_with_failing_pkgfs_fails(pkgfs, pkg, failing_file_call_count).await
}

#[fasync::run_singlethreaded(test)]
async fn test_fails_on_open_blob_in_install_blob() -> Result<(), Error> {
    let (pkgfs, pkg, failing_file_call_count) =
        make_mock_pkgfs_with_failing_install_blob(handle_file_stream_fail_on_open).await?;

    assert_resolve_package_with_failing_pkgfs_fails(pkgfs, pkg, failing_file_call_count).await
}

#[fasync::run_singlethreaded(test)]
async fn test_fails_truncate_blob_in_install_blob() -> Result<(), Error> {
    let (pkgfs, pkg, failing_file_call_count) =
        make_mock_pkgfs_with_failing_install_blob(handle_file_stream_fail_truncate).await?;

    assert_resolve_package_with_failing_pkgfs_fails(pkgfs, pkg, failing_file_call_count).await
}

#[fasync::run_singlethreaded(test)]
async fn test_fails_write_blob_in_install_blob() -> Result<(), Error> {
    let (pkgfs, pkg, failing_file_call_count) =
        make_mock_pkgfs_with_failing_install_blob(handle_file_stream_fail_write).await?;

    assert_resolve_package_with_failing_pkgfs_fails(pkgfs, pkg, failing_file_call_count).await
}
