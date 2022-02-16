// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    assert_matches::assert_matches,
    async_trait::async_trait,
    blobfs_ramdisk::BlobfsRamdisk,
    fidl_fuchsia_io::{
        NodeMarker, OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE, RX_STAR_DIR,
        R_STAR_DIR,
    },
    fidl_fuchsia_sys2::*,
    fuchsia_component_test::new::{
        Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route,
    },
    fuchsia_zircon as zx,
    futures::{
        channel::oneshot,
        future::{BoxFuture, FutureExt as _, Shared},
        stream::TryStreamExt as _,
    },
    std::sync::Arc,
    vfs::{
        directory::{connection::io1::DerivedConnection as _, entry::DirectoryEntry},
        execution_scope::ExecutionScope,
    },
};

static BASE_RESOLVER_URL: &str =
    "fuchsia-pkg://fuchsia.com/base-resolver-integration-tests#meta/base-resolver.cm";
static PKGFS_BOOT_ARG_KEY: &'static str = "zircon.system.pkgfs.cmd";
static PKGFS_BOOT_ARG_VALUE_PREFIX: &'static str = "bin/pkgsvr+";
static PKG_CACHE_COMPONENT_URL: &'static str = "fuchsia-pkg-cache:///#meta/pkg-cache.cm";

trait BootArgumentsStreamHandler: Send + Sync {
    fn handle_stream(
        &self,
        stream: fidl_fuchsia_boot::ArgumentsRequestStream,
    ) -> BoxFuture<'static, ()>;
}

struct TestEnvBuilder {
    blobfs: Arc<dyn DirectoryEntry>,
    boot_args: Arc<dyn BootArgumentsStreamHandler>,
    minfs: Option<Arc<dyn DirectoryEntry>>,
}

impl TestEnvBuilder {
    fn new(
        blobfs: Arc<dyn DirectoryEntry>,
        boot_args: Arc<dyn BootArgumentsStreamHandler>,
    ) -> Self {
        Self { blobfs, boot_args, minfs: None }
    }

    fn minfs(mut self, minfs: Arc<dyn DirectoryEntry>) -> Self {
        assert!(self.minfs.is_none());
        self.minfs = Some(minfs);
        self
    }

    async fn build(self) -> TestEnv {
        let blobfs = self.blobfs;
        let boot_args = self.boot_args;
        let minfs = self.minfs.unwrap_or_else(|| vfs::pseudo_directory! {});

        let builder = RealmBuilder::new().await.unwrap();

        let base_resolver = builder
            .add_child("base_resolver", BASE_RESOLVER_URL, ChildOptions::new())
            .await
            .unwrap();

        let local_mocks = builder
            .add_local_child(
                "local_mocks",
                move |handles| {
                    let blobfs_clone = blobfs.clone();
                    let boot_args_clone = boot_args.clone();
                    let minfs_clone = minfs.clone();
                    let out_dir = vfs::pseudo_directory! {
                        "blob" => blobfs_clone,
                        "minfs-delayed" => minfs_clone,
                        "svc" => vfs::pseudo_directory! {
                            "fuchsia.boot.Arguments" =>
                                vfs::service::host(move |stream|
                                    boot_args_clone.handle_stream(stream)
                                ),
                        },
                    };
                    let scope = ExecutionScope::new();
                    let () = out_dir.open(
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

        builder
            .add_route(
                Route::new()
                    .capability(Capability::directory("blob").path("/blob").rights(RX_STAR_DIR))
                    .capability(
                        Capability::directory("pkgfs-packages-delayed")
                            .path("/pkgfs-packages")
                            .rights(RX_STAR_DIR),
                    )
                    .capability(
                        Capability::directory("minfs-delayed")
                            .path("/minfs-delayed")
                            .rights(R_STAR_DIR),
                    )
                    .capability(Capability::protocol_by_name("fuchsia.boot.Arguments"))
                    .from(&local_mocks)
                    .to(&base_resolver),
            )
            .await
            .unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&base_resolver),
            )
            .await
            .unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(
                        "fuchsia.sys2.ComponentResolver-ForPkgCache",
                    ))
                    .from(&base_resolver)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();

        TestEnv { realm_instance: builder.build().await.unwrap() }
    }
}

struct TestEnv {
    realm_instance: RealmInstance,
}

impl TestEnv {
    fn pkg_cache_resolver(&self) -> ComponentResolverProxy {
        self.realm_instance
            .root
            .connect_to_named_protocol_at_exposed_dir::<ComponentResolverMarker>(
                "fuchsia.sys2.ComponentResolver-ForPkgCache",
            )
            .unwrap()
    }
}

// Responds to requests for "zircon.system.pkgfs.cmd" with the provided hash.
struct BootArgsFixedHash {
    hash: fuchsia_hash::Hash,
}

impl BootArgsFixedHash {
    fn new(hash: fuchsia_hash::Hash) -> Self {
        Self { hash }
    }
}

impl BootArgumentsStreamHandler for BootArgsFixedHash {
    fn handle_stream(
        &self,
        mut stream: fidl_fuchsia_boot::ArgumentsRequestStream,
    ) -> BoxFuture<'static, ()> {
        let hash = self.hash;
        async move {
            while let Some(request) = stream.try_next().await.unwrap() {
                match request {
                    fidl_fuchsia_boot::ArgumentsRequest::GetString { key, responder } => {
                        assert_eq!(key, PKGFS_BOOT_ARG_KEY);
                        responder
                            .send(Some(&format!("{}{}", PKGFS_BOOT_ARG_VALUE_PREFIX, hash)))
                            .unwrap();
                    }
                    req => panic!("unexpected request {:?}", req),
                }
            }
        }
        .boxed()
    }
}

// The test package these tests are in includes the pkg-cache component. This function creates
// a BlobfsRamdisk and copies into it:
//   1. this test package (and therefore everything needed for pkg-cache)
//   2. a system_image package with a data/static_packages file that says the hash of pkg-cache
//      is the hash of this test package
//
// This function also creates a BootArgsFixedHash that reports the hash of the system_image
// package as that of the system_image package this function wrote to the returned BlobfsRamdisk.
async fn create_blobfs_and_boot_args_from_this_package() -> (BlobfsRamdisk, BootArgsFixedHash) {
    let this_pkg = fuchsia_pkg_testing::Package::identity().await.unwrap();

    let static_packages = system_image::StaticPackages::from_entries(vec![(
        "pkg-cache/0".parse().unwrap(),
        *this_pkg.meta_far_merkle_root(),
    )]);
    let mut static_packages_bytes = vec![];
    static_packages.serialize(&mut static_packages_bytes).expect("write static_packages");
    let system_image = fuchsia_pkg_testing::PackageBuilder::new("system_image")
        .add_resource_at("data/static_packages", static_packages_bytes.as_slice())
        .build()
        .await
        .expect("build system_image package");
    let blobfs = BlobfsRamdisk::start().expect("start blobfs");
    let () = system_image.write_to_blobfs_dir(&blobfs.root_dir().unwrap());

    let () = this_pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    (blobfs, BootArgsFixedHash::new(*system_image.meta_far_merkle_root()))
}

// Signals when Self is opened, then blocks until signalled.
#[derive(Clone)]
struct MinfsSyncOnOpenSelf {
    sync_point: SyncPoint,
}

impl MinfsSyncOnOpenSelf {
    fn new() -> (Self, SyncPoint) {
        let sync_point = SyncPoint::new();
        (Self { sync_point: sync_point.clone() }, sync_point)
    }

    fn directory_entry(&self) -> Arc<dyn DirectoryEntry> {
        Arc::new(self.clone())
    }
}

impl vfs::directory::entry::DirectoryEntry for MinfsSyncOnOpenSelf {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        _mode: u32,
        path: vfs::path::Path,
        server_end: fidl::endpoints::ServerEnd<NodeMarker>,
    ) {
        if path.is_empty() {
            let sync_point = self.sync_point.clone();
            let () = scope.clone().spawn(async move {
                let () = sync_point.signal_reached_then_wait_until_can_continue().await;
                let () =
                vfs::directory::immutable::connection::io1::ImmutableConnection::create_connection(
                    scope, self, flags, server_end,
                );
            });
            return;
        }
        let () = vfs::common::send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND);
    }

    fn entry_info(&self) -> vfs::directory::entry::EntryInfo {
        vfs::directory::entry::EntryInfo::new(
            fidl_fuchsia_io::INO_UNKNOWN,
            fidl_fuchsia_io::DIRENT_TYPE_DIRECTORY,
        )
    }
}

#[async_trait]
impl vfs::directory::entry_container::Directory for MinfsSyncOnOpenSelf {
    async fn read_dirents<'a>(
        &'a self,
        pos: &'a vfs::directory::traversal_position::TraversalPosition,
        sink: Box<(dyn vfs::directory::dirents_sink::Sink + 'static)>,
    ) -> Result<
        (
            vfs::directory::traversal_position::TraversalPosition,
            Box<(dyn vfs::directory::dirents_sink::Sealed + 'static)>,
        ),
        zx::Status,
    > {
        let sink = match pos {
            vfs::directory::traversal_position::TraversalPosition::Start => {
                match sink.append(
                    &vfs::directory::entry::EntryInfo::new(
                        0,
                        fidl_fuchsia_io::DIRENT_TYPE_DIRECTORY,
                    ),
                    ".",
                ) {
                    vfs::directory::dirents_sink::AppendResult::Ok(sink) => (sink),
                    vfs::directory::dirents_sink::AppendResult::Sealed(_) => {
                        panic!("BlockingMinfs has no contents, writing '.' should succeed")
                    }
                }
            }
            _ => panic!("BlockingMinfs has no contents, readdir shouldn't be paging"),
        };
        Ok((vfs::directory::traversal_position::TraversalPosition::End, sink.seal().into()))
    }

    fn register_watcher(
        self: Arc<Self>,
        _: ExecutionScope,
        _: u32,
        _: fidl::AsyncChannel,
    ) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    fn unregister_watcher(self: Arc<Self>, _: usize) {}

    async fn get_attrs(&self) -> Result<fidl_fuchsia_io::NodeAttributes, zx::Status> {
        Ok(fidl_fuchsia_io::NodeAttributes {
            mode: fidl_fuchsia_io::MODE_TYPE_DIRECTORY,
            id: 1,
            content_size: 0,
            storage_size: 0,
            link_count: 1,
            creation_time: 0,
            modification_time: 0,
        })
    }

    fn close(&self) -> Result<(), zx::Status> {
        Ok(())
    }
}

// Allows synchronizing two async processes, P0 and P1, around the first time event E occurs in P1:
//   * P0 performs `sync_point.wait_until_reached().await` to block until E occurs
//   * P1 performs `sync_point.signal_reached_then_wait_until_can_continue().await` to:
//       1. signal that E has occurred
//       2. block until P0 is ready
//   * P0 performs `sync_point.signal_can_continue()` to signal that it is aware E has occurred and
//     P1 can continue
#[derive(Clone)]
struct SyncPoint {
    reached: Shared<oneshot::Receiver<()>>,
    reached_signaller: Arc<futures::lock::Mutex<Option<oneshot::Sender<()>>>>,
    can_continue: Shared<oneshot::Receiver<()>>,
    can_continue_signaller: Arc<futures::lock::Mutex<Option<oneshot::Sender<()>>>>,
}

impl SyncPoint {
    fn new() -> Self {
        let (reached_signaller, reached) = oneshot::channel();
        let reached_signaller = Arc::new(futures::lock::Mutex::new(Some(reached_signaller)));
        let reached = reached.shared();

        let (can_continue_signaller, can_continue) = oneshot::channel();
        let can_continue_signaller =
            Arc::new(futures::lock::Mutex::new(Some(can_continue_signaller)));
        let can_continue = can_continue.shared();

        Self { reached, reached_signaller, can_continue, can_continue_signaller }
    }

    fn wait_until_reached(&self) -> impl futures::Future<Output = ()> {
        self.reached.clone().map(|res| res.expect("never cancelled"))
    }

    async fn signal_reached_then_wait_until_can_continue(&self) {
        if let Some(sender) = self.reached_signaller.lock().await.take() {
            sender.send(()).unwrap()
        }
        let () = self.can_continue.clone().await.unwrap();
    }

    async fn signal_can_continue(&self) {
        if let Some(sender) = self.can_continue_signaller.lock().await.take() {
            sender.send(()).unwrap()
        }
    }
}

#[fuchsia::test]
async fn pkg_cache_resolver_waits_for_minfs() {
    let (blobfs, boot_args) = create_blobfs_and_boot_args_from_this_package().await;
    let (minfs, minfs_sync_point) = MinfsSyncOnOpenSelf::new();

    let env = TestEnvBuilder::new(
        vfs::remote::remote_dir(blobfs.root_dir_proxy().unwrap()),
        Arc::new(boot_args),
    )
    .minfs(minfs.directory_entry())
    .build()
    .await;

    let mut minfs_opened = minfs_sync_point.wait_until_reached().fuse();
    let mut resolve = env.pkg_cache_resolver().resolve(PKG_CACHE_COMPONENT_URL).fuse();
    let () = futures::select! {
        () = minfs_opened => (),
        _ = resolve => panic!("resolve should not succeed before minfs open attempt")
    };

    let mut timeout = fuchsia_async::Timer::new(std::time::Duration::from_millis(100)).fuse();
    let () = futures::select! {
        () = timeout => (),
        _ = resolve => panic!("resolve should not succeed before minfs unblocked")
    };

    let () = minfs_sync_point.signal_can_continue().await;
    assert_matches!(resolve.await.unwrap(), Ok(Component { .. }));
}
