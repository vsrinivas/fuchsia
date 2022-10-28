// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    device_watcher::recursive_wait_and_open_node,
    fidl::endpoints::{create_proxy, Proxy},
    fidl_fuchsia_boot as fboot,
    fidl_fuchsia_fxfs::{CryptManagementMarker, CryptMarker, KeyPurpose},
    fidl_fuchsia_io as fio, fidl_fuchsia_logger as flogger, fidl_fuchsia_process as fprocess,
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route},
    fuchsia_zircon as zx,
    futures::FutureExt,
    key_bag::Aes256Key,
    ramdevice_client::{RamdiskClient, VmoRamdiskClientBuilder},
    std::ops::Deref,
};

pub mod disk_builder;
pub mod fshost_builder;
mod mocks;

pub const FVM_SLICE_SIZE: u64 = 32 * 1024;

pub async fn create_hermetic_crypt_service(
    data_key: Aes256Key,
    metadata_key: Aes256Key,
) -> RealmInstance {
    let builder = RealmBuilder::new().await.unwrap();
    let url = "#meta/fxfs-crypt.cm";
    let crypt = builder.add_child("fxfs-crypt", url, ChildOptions::new().eager()).await.unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<CryptMarker>())
                .capability(Capability::protocol::<CryptManagementMarker>())
                .from(&crypt)
                .to(Ref::parent()),
        )
        .await
        .unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<flogger::LogSinkMarker>())
                .from(Ref::parent())
                .to(&crypt),
        )
        .await
        .unwrap();
    let realm = builder.build().await.expect("realm build failed");
    let crypt_management =
        realm.root.connect_to_protocol_at_exposed_dir::<CryptManagementMarker>().unwrap();
    crypt_management
        .add_wrapping_key(0, data_key.deref())
        .await
        .unwrap()
        .expect("add_wrapping_key failed");
    crypt_management
        .add_wrapping_key(1, metadata_key.deref())
        .await
        .unwrap()
        .expect("add_wrapping_key failed");
    crypt_management
        .set_active_key(KeyPurpose::Data, 0)
        .await
        .unwrap()
        .expect("set_active_key failed");
    crypt_management
        .set_active_key(KeyPurpose::Metadata, 1)
        .await
        .unwrap()
        .expect("set_active_key failed");
    realm
}

pub struct TestFixtureBuilder {
    netboot: bool,

    disk: Option<disk_builder::DiskBuilder>,
    fshost: fshost_builder::FshostBuilder,
}

impl TestFixtureBuilder {
    pub fn new(fshost_component_name: &'static str) -> Self {
        Self {
            netboot: false,
            disk: None,
            fshost: fshost_builder::FshostBuilder::new(fshost_component_name),
        }
    }

    pub fn fshost(&mut self) -> &mut fshost_builder::FshostBuilder {
        &mut self.fshost
    }

    pub fn with_disk(&mut self) -> &mut disk_builder::DiskBuilder {
        self.disk = Some(disk_builder::DiskBuilder::new());
        self.disk.as_mut().unwrap()
    }

    pub fn netboot(mut self) -> Self {
        self.netboot = true;
        self
    }

    pub async fn build(self) -> TestFixture {
        let mocks = mocks::new_mocks(self.netboot).await;
        let builder = RealmBuilder::new().await.unwrap();
        let fshost = self.fshost.build(&builder).await;

        let mocks = builder
            .add_local_child("mocks", move |h| mocks(h).boxed(), ChildOptions::new())
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

        if let Some(disk) = self.disk {
            let vmo = disk.build().await;
            let vmo_clone =
                vmo.create_child(zx::VmoChildOptions::SLICE, 0, vmo.get_size().unwrap()).unwrap();

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
                        .capability(
                            Capability::directory("dev-topological").rights(fio::RW_STAR_DIR),
                        )
                        .from(&drivers)
                        .to(Ref::parent())
                        .to(&fshost),
                )
                .await
                .unwrap();
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol::<fprocess::LauncherMarker>())
                        .capability(Capability::protocol::<flogger::LogSinkMarker>())
                        .from(Ref::parent())
                        .to(&drivers),
                )
                .await
                .unwrap();

            let mut fixture = TestFixture {
                realm: builder.build().await.unwrap(),
                ramdisk: None,
                ramdisk_vmo: None,
            };

            let dev = fixture.dir("dev-topological");

            recursive_wait_and_open_node(&dev, "sys/platform/00:00:2d/ramctl")
                .await
                .expect("recursive_wait_and_open_node failed");

            let dev_fd =
                fdio::create_fd(dev.into_channel().unwrap().into_zx_channel().into()).unwrap();

            fixture.ramdisk = Some(
                VmoRamdiskClientBuilder::new(vmo).dev_root(dev_fd).block_size(512).build().unwrap(),
            );
            fixture.ramdisk_vmo = Some(vmo_clone);

            fixture
        } else {
            builder
                .add_route(
                    Route::new()
                        .capability(
                            Capability::directory("dev-topological")
                                .path("/dev")
                                .rights(fio::RW_STAR_DIR),
                        )
                        .from(&mocks)
                        .to(&fshost)
                        .to(Ref::parent()),
                )
                .await
                .unwrap();
            TestFixture { realm: builder.build().await.unwrap(), ramdisk: None, ramdisk_vmo: None }
        }
    }
}

pub struct TestFixture {
    pub realm: RealmInstance,
    pub ramdisk: Option<RamdiskClient>,
    pub ramdisk_vmo: Option<zx::Vmo>,
}

impl TestFixture {
    pub async fn tear_down(self) {
        self.realm.destroy().await.unwrap();
    }

    pub fn dir(&self, dir: &str) -> fio::DirectoryProxy {
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

    pub async fn check_fs_type(&self, dir: &str, fs_type: u32) {
        let (status, info) = self.dir(dir).query_filesystem().await.expect("query failed");
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        assert!(info.is_some());
        assert_eq!(info.unwrap().fs_type, fs_type);
    }

    pub fn ramdisk_vmo(&self) -> Option<&zx::Vmo> {
        self.ramdisk_vmo.as_ref()
    }
}
