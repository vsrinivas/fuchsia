// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_driver_test::DriverTestRealmBuilder;
use futures::FutureExt;
use vfs::directory::entry::DirectoryEntry;
use {
    anyhow::Error,
    fidl::endpoints::{Proxy, ServerEnd},
    fidl_fuchsia_driver_test as fdt, fidl_fuchsia_io as fio,
    fuchsia_component_test::{
        Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route,
    },
    fuchsia_driver_test::DriverTestRealmInstance,
};

type Directory = std::sync::Arc<
    vfs::directory::simple::Simple<vfs::directory::immutable::connection::io1::ImmutableConnection>,
>;

async fn serve_fake_filesystem(
    system: Directory,
    pkgfs: Directory,
    handles: LocalComponentHandles,
) -> Result<(), anyhow::Error> {
    let fs_scope = vfs::execution_scope::ExecutionScope::new();
    let root: Directory = vfs::pseudo_directory! {
        "pkgfs" => pkgfs,
        "system" => system,
        "boot" => vfs::pseudo_directory! {
            "meta" => vfs::pseudo_directory! {},
        },
    };
    root.open(
        fs_scope.clone(),
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        0,
        vfs::path::Path::dot(),
        ServerEnd::new(handles.outgoing_dir.into_channel()),
    );
    fs_scope.wait().await;
    Ok::<(), anyhow::Error>(())
}

async fn create_realm(
    system: Directory,
    pkgfs: Directory,
) -> Result<fuchsia_component_test::RealmInstance, Error> {
    let builder = RealmBuilder::new().await?;

    let fake_filesystem = builder
        .add_local_child(
            "fake_filesystem",
            move |h: LocalComponentHandles| {
                serve_fake_filesystem(system.clone(), pkgfs.clone(), h).boxed()
            },
            ChildOptions::new().eager(),
        )
        .await
        .expect("mock component added");

    let driver_manager = builder
        .add_child(
            "driver_manager",
            "fuchsia-pkg://fuchsia.com/ddk-firmware-test#meta/driver-manager-realm.cm",
            ChildOptions::new(),
        )
        .await?;

    builder
        .add_route(
            Route::new()
                .capability(
                    Capability::directory("pkgfs-delayed").path("/pkgfs").rights(fio::RX_STAR_DIR),
                )
                .capability(
                    Capability::directory("pkgfs-packages-delayed")
                        .path("/pkgfs/packages")
                        .rights(fio::R_STAR_DIR),
                )
                .capability(
                    Capability::directory("system-delayed")
                        .path("/system")
                        .rights(fio::RX_STAR_DIR),
                )
                .capability(Capability::directory("boot").path("/boot").rights(fio::R_STAR_DIR))
                .from(&fake_filesystem)
                .to(&driver_manager),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::directory("dev"))
                .capability(Capability::protocol_by_name("fuchsia.device.manager.Administrator"))
                .capability(Capability::protocol_by_name("fuchsia.driver.test.Realm"))
                .from(&driver_manager)
                .to(Ref::parent()),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .capability(Capability::protocol_by_name("fuchsia.process.Launcher"))
                .capability(Capability::protocol_by_name("fuchsia.sys.Launcher"))
                .from(Ref::parent())
                .to(&driver_manager),
        )
        .await?;
    let realm = builder.build().await?;
    realm.driver_test_realm_start(fdt::RealmArgs::EMPTY).await?;
    Ok(realm)
}

#[fuchsia::test]
async fn load_package_firmware_test() -> Result<(), Error> {
    let firmware_file = vfs::file::vmo::asynchronous::read_only_static(b"this is some firmware\n");
    let system: Directory = vfs::pseudo_directory! {
        "driver" => vfs::pseudo_directory! {},
        "lib" => vfs::pseudo_directory! {
            "firmware" => vfs::pseudo_directory! {
                "system-firmware" => firmware_file.clone(),
            },
        },
    };
    let driver_dir = vfs::remote::remote_dir(io_util::open_directory_in_namespace(
        "/pkg/driver",
        io_util::OpenFlags::RIGHT_READABLE | io_util::OpenFlags::RIGHT_EXECUTABLE,
    )?);
    let meta_dir = vfs::remote::remote_dir(io_util::open_directory_in_namespace(
        "/pkg/meta",
        io_util::OpenFlags::RIGHT_READABLE,
    )?);
    let bind_dir = vfs::remote::remote_dir(io_util::open_directory_in_namespace(
        "/pkg/bind",
        io_util::OpenFlags::RIGHT_READABLE,
    )?);
    let base_manifest = vfs::file::vmo::asynchronous::read_only_static(
        r#"[{"driver_url": "fuchsia-pkg://fuchsia.com/my-package#meta/ddk-firmware-test-driver.cm"}]"#,
    );
    let pkgfs = vfs::pseudo_directory! {
        "packages" => vfs::pseudo_directory! {
            "driver-manager-base-config" => vfs::pseudo_directory! {
                "0" => vfs::pseudo_directory! {
                    "config" => vfs::pseudo_directory! {
                        "base-driver-manifest.json" => base_manifest,
                    },
                },
            },
            "my-package" => vfs::pseudo_directory! {
                "0" => vfs::pseudo_directory! {
                    "driver" => driver_dir,
                    "meta" => meta_dir,
                    "bind" => bind_dir,
                        "lib" => vfs::pseudo_directory! {
                            "firmware" => vfs::pseudo_directory! {
                                "package-firmware" => firmware_file,
                            },
                        },
                    },
                },
            },
    };

    let instance = create_realm(system, pkgfs).await?;

    // This is unused but connecting to it causes DriverManager to start.
    let _admin = instance
        .root
        .connect_to_protocol_at_exposed_dir::<fidl_fuchsia_device_manager::AdministratorMarker>()?;

    let out_dir = instance.root.get_exposed_dir();
    let driver_service =
        device_watcher::recursive_wait_and_open_node(&out_dir, "dev/sys/test/ddk-firmware-test")
            .await?;
    let driver_proxy = fidl_fuchsia_device_firmware_test::TestDeviceProxy::from_channel(
        driver_service.into_channel().unwrap(),
    );

    // Check that we can load firmware out of /boot.
    driver_proxy.load_firmware("test-firmware").await?.unwrap();
    driver_proxy.load_firmware_async("test-firmware").await?.unwrap();

    // Check that we can't load system-firmware.
    assert!(
        driver_proxy.load_firmware("system-firmware").await?
            == Err(fuchsia_zircon::sys::ZX_ERR_NOT_FOUND)
    );
    assert!(
        driver_proxy.load_firmware_async("system-firmware").await?
            == Err(fuchsia_zircon::sys::ZX_ERR_NOT_FOUND)
    );

    // Check that we can load firmware from our package.
    driver_proxy.load_firmware("package-firmware").await?.unwrap();
    driver_proxy.load_firmware_async("package-firmware").await?.unwrap();

    // Check that loading unknown name fails.
    assert!(
        driver_proxy.load_firmware("test-bad").await? == Err(fuchsia_zircon::sys::ZX_ERR_NOT_FOUND)
    );
    assert!(
        driver_proxy.load_firmware_async("test-bad").await?
            == Err(fuchsia_zircon::sys::ZX_ERR_NOT_FOUND)
    );
    Ok(())
}

#[fuchsia::test]
async fn load_package_firmware_test_dfv2() -> Result<(), Error> {
    // Create the RealmBuilder.
    let builder = RealmBuilder::new().await?;
    builder.driver_test_realm_setup().await?;
    let instance = builder.build().await?;

    // Start DriverTestRealm
    let args = fdt::RealmArgs {
        use_driver_framework_v2: Some(true),
        root_driver: Some("fuchsia-boot:///#meta/test-parent-sys.cm".to_string()),
        ..fdt::RealmArgs::EMPTY
    };
    instance.driver_test_realm_start(args).await?;

    // Connect to our driver.
    let dev = instance.driver_test_realm_connect_to_dev()?;
    let driver_service =
        device_watcher::recursive_wait_and_open_node(&dev, "sys/test/ddk-firmware-test").await?;
    let driver_proxy = fidl_fuchsia_device_firmware_test::TestDeviceProxy::from_channel(
        driver_service.into_channel().unwrap(),
    );

    // Check that we can load firmware from our package.
    driver_proxy.load_firmware("test-firmware").await?.unwrap();
    driver_proxy.load_firmware_async("test-firmware").await?.unwrap();

    // Check that loading unknown name fails.
    assert_eq!(
        driver_proxy.load_firmware("test-bad").await?,
        Err(fuchsia_zircon::sys::ZX_ERR_NOT_FOUND)
    );
    assert_eq!(
        driver_proxy.load_firmware_async("test-bad").await?,
        Err(fuchsia_zircon::sys::ZX_ERR_NOT_FOUND)
    );
    Ok(())
}

#[fuchsia::test]
async fn load_system_firmware_test() -> Result<(), Error> {
    let firmware_file = vfs::file::vmo::asynchronous::read_only_static(b"this is some firmware\n");
    let driver_dir = vfs::remote::remote_dir(io_util::open_directory_in_namespace(
        "/pkg/driver",
        io_util::OpenFlags::RIGHT_READABLE | io_util::OpenFlags::RIGHT_EXECUTABLE,
    )?);
    let system: Directory = vfs::pseudo_directory! {
        "driver" => driver_dir,
        "lib" => vfs::pseudo_directory! {
            "firmware" => vfs::pseudo_directory! {
                "system-firmware" => firmware_file.clone(),
            },
        },
    };
    let base_manifest = vfs::file::vmo::asynchronous::read_only_static(r#"[]"#);
    let pkgfs = vfs::pseudo_directory! {
        "packages" => vfs::pseudo_directory! {
            "driver-manager-base-config" => vfs::pseudo_directory! {
                "0" => vfs::pseudo_directory! {
                    "config" => vfs::pseudo_directory! {
                        "base-driver-manifest.json" => base_manifest,
                    },
                },
            },
        },
    };

    let instance = create_realm(system, pkgfs).await?;

    // This is unused but connecting to it causes DriverManager to start.
    let _admin = instance
        .root
        .connect_to_protocol_at_exposed_dir::<fidl_fuchsia_device_manager::AdministratorMarker>()?;

    let out_dir = instance.root.get_exposed_dir();
    let driver_service =
        device_watcher::recursive_wait_and_open_node(&out_dir, "dev/sys/test/ddk-firmware-test")
            .await?;
    let driver_proxy = fidl_fuchsia_device_firmware_test::TestDeviceProxy::from_channel(
        driver_service.into_channel().unwrap(),
    );

    // Check that we can load firmware out of /boot.
    driver_proxy.load_firmware("test-firmware").await?.unwrap();
    driver_proxy.load_firmware_async("test-firmware").await?.unwrap();

    // Check that the system driver can load system-firmware.
    driver_proxy.load_firmware("system-firmware").await?.unwrap();
    driver_proxy.load_firmware_async("system-firmware").await?.unwrap();

    // Check that the system driver can't load package-firmware.
    assert!(
        driver_proxy.load_firmware("package-firmware").await?
            == Err(fuchsia_zircon::sys::ZX_ERR_NOT_FOUND)
    );
    assert!(
        driver_proxy.load_firmware_async("package-firmware").await?
            == Err(fuchsia_zircon::sys::ZX_ERR_NOT_FOUND)
    );

    // Check that loading unknown name fails.
    assert!(
        driver_proxy.load_firmware("test-bad").await? == Err(fuchsia_zircon::sys::ZX_ERR_NOT_FOUND)
    );
    assert!(
        driver_proxy.load_firmware_async("test-bad").await?
            == Err(fuchsia_zircon::sys::ZX_ERR_NOT_FOUND)
    );
    Ok(())
}
