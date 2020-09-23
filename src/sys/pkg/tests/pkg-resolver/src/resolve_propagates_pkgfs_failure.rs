// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests the property that pkg_resolver propagates pkgfs errors when
/// servicing fuchsia.pkg.PackageResolver.Resolve FIDL requests.
use {
    anyhow::Error,
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, FileControlHandle, FileMarker, FileObject, FileRequest,
        FileRequestStream, NodeInfo, NodeMarker, DIRENT_TYPE_FILE, INO_UNKNOWN,
    },
    fuchsia_async as fasync,
    fuchsia_merkle::MerkleTree,
    fuchsia_pkg_testing::{Package, RepositoryBuilder},
    fuchsia_vfs_pseudo_fs::{
        directory::entry::DirectoryEntry, directory::entry::EntryInfo, pseudo_directory,
    },
    fuchsia_zircon::Status,
    futures::prelude::*,
    futures::{
        future::FusedFuture,
        task::{Context, Poll},
    },
    lib::{extra_blob_contents, make_pkg_with_extra_blobs, PkgFs, TestEnvBuilder, EMPTY_REPO_PATH},
    matches::assert_matches,
    std::{
        pin::Pin,
        sync::{atomic::AtomicU64, Arc},
    },
};

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
        fasync::Task::spawn(async move {
            directory_entry.await;
        })
        .detach();
        Self {
            root_dir_proxy: DirectoryProxy::new(client.into_channel().expect("proxy to channel")),
        }
    }
}

impl PkgFs for MockPkgFs {
    fn root_dir_handle(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
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
    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Self::Output> {
        Poll::Pending
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
        fasync::Task::spawn(async move {
            let fut = (handler.stream_handler)(handler.call_count, stream, ch);
            fut.await;
        })
        .detach();
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
        Some(&mut NodeInfo::File(FileObject { event: None, stream: None })),
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
        Some(&mut NodeInfo::File(FileObject { event: None, stream: None })),
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
        Some(&mut NodeInfo::File(FileObject { event: None, stream: None })),
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
        FileRequest::Close { responder } => {
            // cache.rs download_blob sends Close then immediately closes the channel.
            let _ = responder.send(Status::OK.into_raw());
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
        FileRequest::Close { responder } => {
            // cache.rs download_blob sends Close then immediately closes the channel.
            let _ = responder.send(Status::OK.into_raw());
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
        FileRequest::Close { responder } => {
            // cache.rs download_blob sends Close then immediately closes the channel.
            let _ = responder.send(Status::OK.into_raw());
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

async fn make_pkg_for_mock_pkgfs_tests(
    package_name: &str,
) -> Result<(Package, String, String), Error> {
    let pkg = make_pkg_with_extra_blobs(package_name, 1).await;
    let pkg_merkle = pkg.meta_far_merkle_root().to_string();
    let blob_merkle = MerkleTree::from_reader(extra_blob_contents(package_name, 0).as_slice())
        .expect("merkle slice")
        .root()
        .to_string();
    Ok((pkg, pkg_merkle, blob_merkle))
}

async fn make_mock_pkgfs_with_failing_install_pkg<StreamHandler, F>(
    package_name: &str,
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
    let (pkg, pkg_merkle, _) = make_pkg_for_mock_pkgfs_tests(package_name).await?;
    let (failing_file, call_count) = FakeFile::new_and_call_count(file_request_stream_handler);
    let d = PkgFsDirectoryBuilder::new().install_pkg(pkg_merkle, failing_file).build();
    Ok((MockPkgFs::new(d), pkg, call_count))
}

async fn make_mock_pkgfs_with_failing_install_blob<StreamHandler, F>(
    package_name: &str,
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
    let (pkg, pkg_merkle, blob_merkle) = make_pkg_for_mock_pkgfs_tests(package_name).await?;
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
    let env = TestEnvBuilder::new().pkgfs(pkgfs).build().await;
    let repo =
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).add_package(&pkg).build().await?;
    let served_repository = Arc::new(repo).server().start()?;
    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);
    let () = env.proxies.repo_manager.add(repo_config.into()).await?.map_err(Status::from_raw)?;

    let res = env.resolve_package(format!("fuchsia-pkg://test/{}", pkg.name()).as_str()).await;

    assert_matches!(res, Err(Status::IO));
    assert_eq!(failing_file_call_count.load(std::sync::atomic::Ordering::SeqCst), 1);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn fails_on_open_far_in_install_pkg() -> Result<(), Error> {
    let (pkgfs, pkg, failing_file_call_count) = make_mock_pkgfs_with_failing_install_pkg(
        "fails_on_open_far_in_install_pkg",
        handle_file_stream_fail_on_open,
    )
    .await?;

    assert_resolve_package_with_failing_pkgfs_fails(pkgfs, pkg, failing_file_call_count).await
}

#[fasync::run_singlethreaded(test)]
async fn fails_truncate_far_in_install_pkg() -> Result<(), Error> {
    let (pkgfs, pkg, failing_file_call_count) = make_mock_pkgfs_with_failing_install_pkg(
        "fails_truncate_far_in_install_pkg",
        handle_file_stream_fail_truncate,
    )
    .await?;

    assert_resolve_package_with_failing_pkgfs_fails(pkgfs, pkg, failing_file_call_count).await
}

#[fasync::run_singlethreaded(test)]
async fn fails_write_far_in_install_pkg() -> Result<(), Error> {
    let (pkgfs, pkg, failing_file_call_count) = make_mock_pkgfs_with_failing_install_pkg(
        "fails_write_far_in_install_pkg",
        handle_file_stream_fail_write,
    )
    .await?;

    assert_resolve_package_with_failing_pkgfs_fails(pkgfs, pkg, failing_file_call_count).await
}

#[fasync::run_singlethreaded(test)]
async fn fails_on_open_blob_in_install_blob() -> Result<(), Error> {
    let (pkgfs, pkg, failing_file_call_count) = make_mock_pkgfs_with_failing_install_blob(
        "fails_on_open_blob_in_install_blob",
        handle_file_stream_fail_on_open,
    )
    .await?;

    assert_resolve_package_with_failing_pkgfs_fails(pkgfs, pkg, failing_file_call_count).await
}

#[fasync::run_singlethreaded(test)]
async fn fails_truncate_blob_in_install_blob() -> Result<(), Error> {
    let (pkgfs, pkg, failing_file_call_count) = make_mock_pkgfs_with_failing_install_blob(
        "fails_truncate_blob_in_install_blob",
        handle_file_stream_fail_truncate,
    )
    .await?;

    assert_resolve_package_with_failing_pkgfs_fails(pkgfs, pkg, failing_file_call_count).await
}

#[fasync::run_singlethreaded(test)]
async fn fails_write_blob_in_install_blob() -> Result<(), Error> {
    let (pkgfs, pkg, failing_file_call_count) = make_mock_pkgfs_with_failing_install_blob(
        "fails_write_blob_in_install_blob",
        handle_file_stream_fail_write,
    )
    .await?;

    assert_resolve_package_with_failing_pkgfs_fails(pkgfs, pkg, failing_file_call_count).await
}
