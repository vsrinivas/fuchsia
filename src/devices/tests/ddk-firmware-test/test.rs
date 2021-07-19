// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_io::DirectoryProxy;
use futures::FutureExt;
use futures::TryStreamExt;
use vfs::directory::entry::DirectoryEntry;
use {
    anyhow::Error,
    fidl::endpoints::{Proxy, ServerEnd},
    fidl_fuchsia_io2 as fio2,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::builder::{
        Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint,
    },
    fuchsia_component_test::mock::MockHandles,
    futures::StreamExt,
};

type Directory = std::sync::Arc<
    vfs::directory::simple::Simple<vfs::directory::immutable::connection::io1::ImmutableConnection>,
>;

async fn serve_fake_filesystem(
    system: Directory,
    pkgfs: Directory,
    handles: MockHandles,
) -> Result<(), anyhow::Error> {
    let (pkgfs_proxy, pkgfs_service) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>()?;
    pkgfs.open(
        vfs::execution_scope::ExecutionScope::new(),
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        0,
        vfs::path::Path::dot(),
        ServerEnd::new(pkgfs_service.into_channel()),
    );
    let (system_proxy, system_service) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>()?;
    system.open(
        vfs::execution_scope::ExecutionScope::new(),
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        0,
        vfs::path::Path::dot(),
        ServerEnd::new(system_service.into_channel()),
    );
    let mut fs = ServiceFs::new();
    fs.add_remote("pkgfs", pkgfs_proxy);
    fs.add_remote("system", system_proxy);
    fs.serve_connection(handles.outgoing_dir.into_channel()).expect("serve mock ServiceFs");
    fs.collect::<()>().await;
    Ok::<(), anyhow::Error>(())
}

async fn wait_for_file(dir: &DirectoryProxy, name: &str) -> Result<(), Error> {
    let mut watcher = fuchsia_vfs_watcher::Watcher::new(io_util::clone_directory(
        dir,
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
    )?)
    .await?;
    while let Some(msg) = watcher.try_next().await? {
        if msg.event != fuchsia_vfs_watcher::WatchEvent::EXISTING
            && msg.event != fuchsia_vfs_watcher::WatchEvent::ADD_FILE
        {
            continue;
        }
        if msg.filename.to_str().unwrap() == name {
            return Ok(());
        }
    }
    unreachable!();
}

async fn recursive_open_node(
    initial_dir: &DirectoryProxy,
    name: &str,
) -> Result<fidl_fuchsia_io::NodeProxy, Error> {
    let mut dir = io_util::clone_directory(initial_dir, fidl_fuchsia_io::OPEN_RIGHT_READABLE)?;

    let path = std::path::Path::new(name);
    let components = path.components().collect::<Vec<_>>();

    for i in 0..(components.len() - 1) {
        let component = &components[i];
        match component {
            std::path::Component::Normal(file) => {
                wait_for_file(&dir, file.to_str().unwrap()).await?;
                dir = io_util::open_directory(
                    &dir,
                    std::path::Path::new(file),
                    io_util::OPEN_RIGHT_READABLE,
                )?;
            }
            _ => panic!("Path must contain only normal components"),
        }
    }
    match components[components.len() - 1] {
        std::path::Component::Normal(file) => {
            wait_for_file(&dir, file.to_str().unwrap()).await?;
            io_util::open_node(
                &dir,
                std::path::Path::new(file),
                fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
                fidl_fuchsia_io::MODE_TYPE_SERVICE,
            )
        }
        _ => panic!("Path must contain only normal components"),
    }
}

async fn create_realm(
    system: Directory,
    pkgfs: Directory,
) -> Result<fuchsia_component_test::RealmInstance, Error> {
    let mut builder = RealmBuilder::new().await?;

    builder
        .add_component(
            "fake_filesystem",
            ComponentSource::mock(move |h: MockHandles| {
                serve_fake_filesystem(system.clone(), pkgfs.clone(), h).boxed()
            }),
        )
        .await
        .expect("mock component added");

    builder
        .add_component(
            "driver_manager",
            ComponentSource::url(
                "fuchsia-pkg://fuchsia.com/ddk-firmware-test#meta/driver-manager-realm.cm",
            ),
        )
        .await?;

    let fake_filesystem = RouteEndpoint::component("fake_filesystem");
    let driver_manager = RouteEndpoint::component("driver_manager");
    builder.add_route(CapabilityRoute {
        capability: Capability::directory("pkgfs-delayed", "/pkgfs", fio2::R_STAR_DIR),
        source: fake_filesystem.clone(),
        targets: vec![driver_manager.clone()],
    })?;
    builder.add_route(CapabilityRoute {
        capability: Capability::directory(
            "pkgfs-packages-delayed",
            "/pkgfs/packages",
            fio2::R_STAR_DIR,
        ),
        source: fake_filesystem.clone(),
        targets: vec![driver_manager.clone()],
    })?;
    builder.add_route(CapabilityRoute {
        capability: Capability::directory("system-delayed", "/system", fio2::R_STAR_DIR),
        source: fake_filesystem.clone(),
        targets: vec![driver_manager.clone()],
    })?;

    builder.add_route(CapabilityRoute {
        capability: Capability::directory("dev", "/dev", fio2::R_STAR_DIR),
        source: driver_manager.clone(),
        targets: vec![RouteEndpoint::AboveRoot],
    })?;

    builder.add_route(CapabilityRoute {
        capability: Capability::protocol("fuchsia.device.manager.Administrator"),
        source: driver_manager.clone(),
        targets: vec![RouteEndpoint::AboveRoot],
    })?;

    builder.add_route(CapabilityRoute {
        capability: Capability::protocol("fuchsia.logger.LogSink"),
        source: RouteEndpoint::AboveRoot,
        targets: vec![driver_manager.clone()],
    })?;
    builder.add_route(CapabilityRoute {
        capability: Capability::protocol("fuchsia.process.Launcher"),
        source: RouteEndpoint::AboveRoot,
        targets: vec![driver_manager.clone()],
    })?;
    builder.add_route(CapabilityRoute {
        capability: Capability::protocol("fuchsia.sys.Launcher"),
        source: RouteEndpoint::AboveRoot,
        targets: vec![driver_manager.clone()],
    })?;

    Ok(builder.build().create().await?)
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
    let driver_dir = directory_broker::DirectoryBroker::from_directory_proxy(
        io_util::open_directory_in_namespace("/pkg/driver/test", io_util::OPEN_RIGHT_READABLE)?,
    );
    let meta_dir = directory_broker::DirectoryBroker::from_directory_proxy(
        io_util::open_directory_in_namespace("/pkg/meta", io_util::OPEN_RIGHT_READABLE)?,
    );
    let bind_dir = directory_broker::DirectoryBroker::from_directory_proxy(
        io_util::open_directory_in_namespace("/pkg/bind", io_util::OPEN_RIGHT_READABLE)?,
    );
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
                    "driver" => vfs::pseudo_directory! {
                        "test" => driver_dir,
                    },
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
    let driver_service = recursive_open_node(&out_dir, "dev/test/ddk-firmware-test").await?;
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
async fn load_system_firmware_test() -> Result<(), Error> {
    let firmware_file = vfs::file::vmo::asynchronous::read_only_static(b"this is some firmware\n");
    let driver_dir = directory_broker::DirectoryBroker::from_directory_proxy(
        io_util::open_directory_in_namespace("/pkg/driver/test", io_util::OPEN_RIGHT_READABLE)?,
    );
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
    let driver_service = recursive_open_node(&out_dir, "dev/test/ddk-firmware-test").await?;
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
