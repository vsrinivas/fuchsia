// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    component_events::{
        events::{Event, EventSource, EventSubscription, Stopped},
        matcher::EventMatcher,
    },
    device_watcher::recursive_wait_and_open_node,
    fidl::endpoints::{create_proxy, Proxy},
    fidl_fuchsia_boot as fboot, fidl_fuchsia_fshost as fshost,
    fidl_fuchsia_fxfs::{CryptManagementMarker, CryptMarker, KeyPurpose},
    fidl_fuchsia_io as fio, fidl_fuchsia_logger as flogger, fidl_fuchsia_process as fprocess,
    fidl_fuchsia_sys as fsys,
    fs_management::{Blobfs, Fxfs, BLOBFS_TYPE_GUID, DATA_TYPE_GUID},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route},
    fuchsia_zircon::{self as zx, HandleBased},
    futures::FutureExt,
    ramdevice_client::{RamdiskClient, VmoRamdiskClientBuilder},
    std::{
        path::Path,
        sync::atomic::{AtomicBool, Ordering},
    },
    storage_isolated_driver_manager::fvm::{create_fvm_volume, set_up_fvm},
    uuid::Uuid,
};

mod mocks;

#[cfg(feature = "fshost_cpp")]
const FSHOST_URL: &'static str = "#meta/test-fshost-fxfs.cm";
#[cfg(feature = "fshost_rust")]
const FSHOST_URL: &'static str = "#meta/test-fshost-rust.cm";

#[derive(Default)]
struct TestFixtureBuilder {
    with_ramdisk: bool,
    format_data: bool,
}

impl TestFixtureBuilder {
    fn with_ramdisk(mut self) -> Self {
        self.with_ramdisk = true;
        self
    }

    fn format_data(mut self) -> Self {
        self.format_data = true;
        self
    }

    async fn build(self) -> TestFixture {
        let mocks = mocks::new_mocks().await;
        let builder = RealmBuilder::new().await.unwrap();
        println!("using {} as fshost", FSHOST_URL);
        let fshost =
            builder.add_child("fshost", FSHOST_URL, ChildOptions::new().eager()).await.unwrap();
        let mocks = builder
            .add_local_child("mocks", move |h| mocks(h).boxed(), ChildOptions::new())
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<fshost::AdminMarker>())
                    .from(&fshost)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<flogger::LogSinkMarker>())
                    .from(Ref::parent())
                    .to(&fshost),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<fboot::ArgumentsMarker>())
                    .capability(Capability::protocol::<fboot::ItemsMarker>())
                    .from(&mocks)
                    .to(&fshost),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::directory("blob").rights(fio::RW_STAR_DIR))
                    .capability(Capability::directory("data").rights(fio::RW_STAR_DIR))
                    .from(&fshost)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<fprocess::LauncherMarker>())
                    .from(Ref::parent())
                    .to(&fshost),
            )
            .await
            .unwrap();

        if self.with_ramdisk {
            let vmo = self.build_ramdisk().await;

            let drivers = builder
                .add_child(
                    "storage_driver_test_realm",
                    "#meta/storage_driver_test_realm.cm",
                    ChildOptions::new().eager(),
                )
                .await
                .unwrap();
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::directory("dev").rights(fio::RW_STAR_DIR))
                        .from(&drivers)
                        .to(Ref::parent())
                        .to(&fshost),
                )
                .await
                .unwrap();
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol::<fsys::LauncherMarker>())
                        .capability(Capability::protocol::<fprocess::LauncherMarker>())
                        .capability(Capability::protocol::<flogger::LogSinkMarker>())
                        .from(Ref::parent())
                        .to(&drivers),
                )
                .await
                .unwrap();

            let mut fixture = TestFixture { realm: builder.build().await.unwrap(), ramdisk: None };

            let dev = fixture.dir("dev");

            recursive_wait_and_open_node(&dev, "sys/platform/00:00:2d/ramctl")
                .await
                .expect("recursive_wait_and_open_node failed");

            let dev_fd =
                fdio::create_fd(dev.into_channel().unwrap().into_zx_channel().into()).unwrap();

            fixture.ramdisk = Some(
                VmoRamdiskClientBuilder::new(vmo).dev_root(dev_fd).block_size(512).build().unwrap(),
            );

            fixture
        } else {
            builder
                .add_route(
                    Route::new()
                        .capability(
                            Capability::directory("dev").path("/dev").rights(fio::RW_STAR_DIR),
                        )
                        .from(&mocks)
                        .to(&fshost)
                        .to(Ref::parent()),
                )
                .await
                .unwrap();
            TestFixture { realm: builder.build().await.unwrap(), ramdisk: None }
        }
    }

    async fn build_ramdisk(&self) -> zx::Vmo {
        let (dev, server) = create_proxy::<fio::DirectoryMarker>().unwrap();
        fdio::open(
            "/dev",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            server.into_channel(),
        )
        .unwrap();

        recursive_wait_and_open_node(&dev, "sys/platform/00:00:2d/ramctl")
            .await
            .expect("recursive_wait_and_open_node failed");

        let vmo = zx::Vmo::create(234881024).unwrap();
        let vmo_dup = vmo.duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap();
        let ramdisk = VmoRamdiskClientBuilder::new(vmo).block_size(512).build().unwrap();
        let ramdisk_path = ramdisk.get_path();

        let volume_manager_proxy =
            set_up_fvm(Path::new(ramdisk_path), 32 * 1024).await.expect("set_up_fvm failed");

        create_fvm_volume(
            &volume_manager_proxy,
            "blobfs",
            &BLOBFS_TYPE_GUID,
            Uuid::new_v4().as_bytes(),
            None,
            0,
        )
        .await
        .expect("create_fvm_volume failed");
        let blobfs_path = format!("{}/fvm/blobfs-p-1/block", ramdisk_path);
        recursive_wait_and_open_node(&dev, &blobfs_path.strip_prefix("/dev/").unwrap())
            .await
            .expect("recursive_wait_and_open_node failed");
        let mut blobfs = Blobfs::new(&blobfs_path).expect("new failed");
        blobfs.format().await.expect("format failed");

        create_fvm_volume(
            &volume_manager_proxy,
            "data",
            &DATA_TYPE_GUID,
            Uuid::new_v4().as_bytes(),
            Some(16 * 1024 * 1024),
            0,
        )
        .await
        .expect("create_fvm_volume failed");

        if self.format_data {
            let data_path = format!("{}/fvm/data-p-2/block", ramdisk_path);
            let data = recursive_wait_and_open_node(&dev, data_path.strip_prefix("/dev/").unwrap())
                .await
                .expect("recursive_wait_and_open_node failed");
            init_crypt_service().await.expect("init_crypt_service failed");
            Fxfs::from_channel(data.into_channel().unwrap().into_zx_channel())
                .expect("from_channel failed")
                .format()
                .await
                .expect("format failed");
            let mut fs = Fxfs::new(&data_path)
                .expect("new failed")
                .serve_multi_volume()
                .await
                .expect("serve_multi_volume failed");
            let vol = fs
                .create_volume(
                    "default",
                    Some(
                        connect_to_protocol::<CryptMarker>()
                            .unwrap()
                            .into_channel()
                            .unwrap()
                            .into_zx_channel()
                            .into(),
                    ),
                )
                .await
                .expect("create_volume failed");
            // Create a file called "foo" that tests can test for presence.
            let (file, server) = create_proxy::<fio::NodeMarker>().unwrap();
            vol.root()
                .open(
                    fio::OpenFlags::RIGHT_READABLE
                        | fio::OpenFlags::RIGHT_WRITABLE
                        | fio::OpenFlags::CREATE,
                    0,
                    "foo",
                    server,
                )
                .expect("open failed");
            // We must solicit a response since otherwise shutdown below could race and creation of
            // the file could get dropped.
            file.describe().await.expect("describe failed");
            fs.shutdown().await.expect("shutdown failed");
        }

        ramdisk.destroy().expect("destroy failed");

        vmo_dup
    }
}

async fn init_crypt_service() -> Result<(), Error> {
    static INITIALIZED: AtomicBool = AtomicBool::new(false);
    if INITIALIZED.load(Ordering::SeqCst) {
        return Ok(());
    }
    let crypt_management = connect_to_protocol::<CryptManagementMarker>().unwrap();
    crypt_management
        .add_wrapping_key(
            0,
            &[
                0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
                0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
                0x1e, 0x1f,
            ],
        )
        .await?
        .map_err(zx::Status::from_raw)?;
    crypt_management
        .add_wrapping_key(
            1,
            &[
                0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8, 0xf7, 0xf6, 0xf5, 0xf4, 0xf3, 0xf2,
                0xf1, 0xf0, 0xef, 0xee, 0xed, 0xec, 0xeb, 0xea, 0xe9, 0xe8, 0xe7, 0xe6, 0xe5, 0xe4,
                0xe3, 0xe2, 0xe1, 0xe0,
            ],
        )
        .await?
        .map_err(zx::Status::from_raw)?;
    crypt_management.set_active_key(KeyPurpose::Data, 0).await?.map_err(zx::Status::from_raw)?;
    crypt_management
        .set_active_key(KeyPurpose::Metadata, 1)
        .await?
        .map_err(zx::Status::from_raw)?;
    INITIALIZED.store(true, Ordering::SeqCst);
    Ok(())
}

struct TestFixture {
    realm: RealmInstance,
    ramdisk: Option<RamdiskClient>,
}

impl TestFixture {
    fn dir(&self, dir: &str) -> fio::DirectoryProxy {
        let (dev, server) = create_proxy::<fio::DirectoryMarker>().expect("create_proxy failed");
        self.realm
            .root
            .get_exposed_dir()
            .open(
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                0,
                dir,
                server.into_channel().into(),
            )
            .expect("open failed");
        dev
    }

    async fn tear_down(self) {
        self.realm.destroy().await.unwrap();
    }
}

#[fuchsia::test]
async fn admin_shutdown_shuts_down_fshost() {
    let fixture = TestFixtureBuilder::default().build().await;

    let event_source = EventSource::new().unwrap();
    let mut event_stream =
        event_source.subscribe(vec![EventSubscription::new(vec![Stopped::NAME])]).await.unwrap();

    let admin =
        fixture.realm.root.connect_to_protocol_at_exposed_dir::<fshost::AdminMarker>().unwrap();
    admin.shutdown().await.unwrap();

    EventMatcher::ok()
        .moniker(format!("./realm_builder:{}/fshost", fixture.realm.root.child_name()))
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();

    fixture.tear_down().await;
}

#[fuchsia::test]
async fn blobfs_and_data_mounted() {
    let fixture = TestFixtureBuilder::default().with_ramdisk().format_data().build().await;

    fixture.dir("blob").describe().await.expect("describe failed");

    let (file, server) = create_proxy::<fio::NodeMarker>().unwrap();
    fixture
        .dir("data")
        .open(fio::OpenFlags::RIGHT_READABLE, 0, "foo", server)
        .expect("open failed");
    file.describe().await.expect("describe failed");

    fixture.tear_down().await;
}

#[fuchsia::test]
async fn data_formatted() {
    let fixture = TestFixtureBuilder::default().with_ramdisk().build().await;

    fixture.dir("data").describe().await.expect("describe failed");

    fixture.tear_down().await;
}
