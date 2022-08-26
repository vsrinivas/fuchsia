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
    key_bag::Aes256Key,
    ramdevice_client::{RamdiskClient, VmoRamdiskClientBuilder},
    std::{
        io::Write,
        ops::Deref,
        path::Path,
        sync::atomic::{AtomicBool, Ordering},
    },
    storage_isolated_driver_manager::{
        fvm::{create_fvm_volume, set_up_fvm},
        zxcrypt,
    },
    uuid::Uuid,
};

mod mocks;

const FSHOST_COMPONENT_NAME: &'static str = std::env!("FSHOST_COMPONENT_NAME");
const DATA_FILESYSTEM_FORMAT: &'static str = std::env!("DATA_FILESYSTEM_FORMAT");

fn data_fs_type() -> u32 {
    match DATA_FILESYSTEM_FORMAT {
        "f2fs" => VFS_TYPE_F2FS,
        "fxfs" => VFS_TYPE_FXFS,
        "minfs" => VFS_TYPE_MINFS,
        _ => panic!("invalid data filesystem format"),
    }
}

// We use a static key-bag so that the crypt instance can be shared across test executions safely.
// These keys match the DATA_KEY and METADATA_KEY respectively, when wrapped with the "zxcrypt"
// static key used by fshost.
const KEY_BAG_CONTENTS: &'static str = "\
{
    \"version\":1,
    \"keys\": {
        \"0\":{
            \"Aes128GcmSivWrapped\": [
                \"7a7c6a718cfde7078f6edec5\",
                \"7cc31b765c74db3191e269d2666267022639e758fe3370e8f36c166d888586454fd4de8aeb47aadd81c531b0a0a66f27\"
            ]
        },
        \"1\":{
            \"Aes128GcmSivWrapped\": [
                \"b7d7f459cbee4cc536cc4324\",
                \"9f6a5d894f526b61c5c091e5e02a7ff94d18e6ad36a0aa439c86081b726eca79e6b60bd86ee5d86a20b3df98f5265a99\"
            ]
        }
    }
}";

const DATA_KEY: Aes256Key = Aes256Key::create([
    0xcf, 0x9e, 0x45, 0x2a, 0x22, 0xa5, 0x70, 0x31, 0x33, 0x3b, 0x4d, 0x6b, 0x6f, 0x78, 0x58, 0x29,
    0x04, 0x79, 0xc7, 0xd6, 0xa9, 0x4b, 0xce, 0x82, 0x04, 0x56, 0x5e, 0x82, 0xfc, 0xe7, 0x37, 0xa8,
]);

const METADATA_KEY: Aes256Key = Aes256Key::create([
    0x0f, 0x4d, 0xca, 0x6b, 0x35, 0x0e, 0x85, 0x6a, 0xb3, 0x8c, 0xdd, 0xe9, 0xda, 0x0e, 0xc8, 0x22,
    0x8e, 0xea, 0xd8, 0x05, 0xc4, 0xc9, 0x0b, 0xa8, 0xd8, 0x85, 0x87, 0x50, 0x75, 0x40, 0x1c, 0x4c,
]);

const VFS_TYPE_BLOBFS: u32 = 0x9e694d21;
// const VFS_TYPE_FATFS: u32 = 0xce694d21;
const VFS_TYPE_MINFS: u32 = 0x6e694d21;
// const VFS_TYPE_MEMFS: u32 = 0x3e694d21;
// const VFS_TYPE_FACTORYFS: u32 = 0x1e694d21;
const VFS_TYPE_FXFS: u32 = 0x73667866;
const VFS_TYPE_F2FS: u32 = 0xfe694d21;

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
        let fshost_url = format!("#meta/{}.cm", FSHOST_COMPONENT_NAME);
        println!("using {} as test-fshost", fshost_url);
        let fshost = builder
            .add_child("test-fshost", fshost_url, ChildOptions::new().eager())
            .await
            .unwrap();
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
            init_data(ramdisk_path, &dev).await;
        }

        ramdisk.destroy().expect("destroy failed");

        vmo_dup
    }
}

async fn init_data(ramdisk_path: &str, dev: &fio::DirectoryProxy) {
    match DATA_FILESYSTEM_FORMAT {
        "fxfs" => init_data_fxfs(ramdisk_path, dev).await,
        "minfs" => init_data_minfs(ramdisk_path, dev).await,
        _ => panic!("unsupported data filesystem format type"),
    }
}

async fn init_data_minfs(ramdisk_path: &str, dev: &fio::DirectoryProxy) {
    let data_path = format!("{}/fvm/data-p-2/block", ramdisk_path);
    recursive_wait_and_open_node(&dev, &data_path.strip_prefix("/dev/").unwrap())
        .await
        .expect("recursive_wait_and_open_node failed");
    let zxcrypt_path = zxcrypt::set_up_insecure_zxcrypt(Path::new(&data_path))
        .await
        .expect("failed to set up zxcrypt");
    let zxcrypt_path = zxcrypt_path.as_os_str().to_str().unwrap();
    let data_device =
        recursive_wait_and_open_node(dev, zxcrypt_path.strip_prefix("/dev/").unwrap())
            .await
            .expect("recursive_wait_and_open_node failed");
    let mut minfs =
        fs_management::Minfs::from_channel(data_device.into_channel().unwrap().into_zx_channel())
            .expect("from_channel failed");
    minfs.format().await.expect("format failed");
    let fs = minfs.serve().await.expect("serve_single_volume failed");
    // Create a file called "foo" that tests can test for presence.
    let (file, server) = create_proxy::<fio::NodeMarker>().unwrap();
    fs.root()
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

async fn init_data_fxfs(ramdisk_path: &str, dev: &fio::DirectoryProxy) {
    let data_path = format!("{}/fvm/data-p-2/block", ramdisk_path);
    let data_device = recursive_wait_and_open_node(dev, data_path.strip_prefix("/dev/").unwrap())
        .await
        .expect("recursive_wait_and_open_node failed");
    let mut fxfs = Fxfs::from_channel(data_device.into_channel().unwrap().into_zx_channel())
        .expect("from_channel failed");
    fxfs.format().await.expect("format failed");
    let mut fs = fxfs.serve_multi_volume().await.expect("serve_multi_volume failed");
    let vol = {
        let vol = fs.create_volume("unencrypted", None).await.expect("create_volume failed");
        vol.bind_to_path("/unencrypted_volume").unwrap();
        // Initialize the key-bag with the static keys.
        std::fs::create_dir("/unencrypted_volume/keys").expect("create_dir failed");
        let mut file = std::fs::File::create("/unencrypted_volume/keys/fxfs-data")
            .expect("create file failed");
        file.write_all(KEY_BAG_CONTENTS.as_bytes()).expect("write file failed");

        init_crypt_service().await.expect("init crypt service failed");

        // OK, crypt is seeded with the stored keys, so we can finally open the data volume.
        let crypt_service = Some(
            connect_to_protocol::<CryptMarker>()
                .expect("Unable to connect to Crypt service")
                .into_channel()
                .unwrap()
                .into_zx_channel()
                .into(),
        );
        fs.create_volume("data", crypt_service).await.expect("create_volume failed")
    };
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

async fn init_crypt_service() -> Result<(), Error> {
    static INITIALIZED: AtomicBool = AtomicBool::new(false);
    if INITIALIZED.load(Ordering::SeqCst) {
        return Ok(());
    }
    let crypt_management = connect_to_protocol::<CryptManagementMarker>()?;
    crypt_management.add_wrapping_key(0, DATA_KEY.deref()).await?.map_err(zx::Status::from_raw)?;
    crypt_management
        .add_wrapping_key(1, METADATA_KEY.deref())
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

    async fn check_fs_type(&self, dir: &str, fs_type: u32) {
        let (status, info) = self.dir(dir).query_filesystem().await.expect("query failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        assert!(info.is_some());
        assert_eq!(info.unwrap().fs_type, fs_type);
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
        .moniker(format!("./realm_builder:{}/test-fshost", fixture.realm.root.child_name()))
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();

    fixture.tear_down().await;
}

#[fuchsia::test]
async fn blobfs_and_data_mounted() {
    let fixture = TestFixtureBuilder::default().with_ramdisk().format_data().build().await;

    fixture.check_fs_type("blob", VFS_TYPE_BLOBFS).await;
    fixture.check_fs_type("data", data_fs_type()).await;

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

    fixture.check_fs_type("data", data_fs_type()).await;

    fixture.tear_down().await;
}
