// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    fidl_fuchsia_driver_development as fdd, fidl_fuchsia_driver_test as fdt,
    fuchsia_async as fasync,
    fuchsia_component_test::new::RealmBuilder,
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
};

async fn get_driver_info(
    service: &fdd::DriverDevelopmentProxy,
    driver_filter: &mut dyn ExactSizeIterator<Item = &str>,
) -> Result<Vec<fdd::DriverInfo>> {
    let (iterator, iterator_server) =
        fidl::endpoints::create_proxy::<fdd::DriverInfoIteratorMarker>()?;

    service
        .get_driver_info(driver_filter, iterator_server)
        .context("FIDL call to get driver info failed")?;

    let mut info_result = Vec::new();
    loop {
        let mut driver_info =
            iterator.get_next().await.context("FIDL call to get driver info failed")?;
        if driver_info.len() == 0 {
            break;
        }
        info_result.append(&mut driver_info)
    }
    Ok(info_result)
}

#[fasync::run_singlethreaded(test)]
async fn test_smoke_test() -> Result<()> {
    let realm = RealmBuilder::new().await?;
    realm.driver_test_realm_setup().await?;

    let instance = realm.build().await?;

    instance.driver_test_realm_start(fdt::RealmArgs::EMPTY).await?;

    // Connect to a protocol to ensure that it starts, then immediately exit.
    let _ = instance.root.connect_to_protocol_at_exposed_dir::<fdd::DriverDevelopmentMarker>()?;
    Ok(())
}

// Run DriverTestRealm with no arguments and see that the drivers in our package
// are loaded.
#[fasync::run_singlethreaded(test)]
async fn test_empty_args() -> Result<()> {
    let realm = RealmBuilder::new().await?;
    realm.driver_test_realm_setup().await?;

    let instance = realm.build().await?;

    instance.driver_test_realm_start(fdt::RealmArgs::EMPTY).await?;

    let driver_dev =
        instance.root.connect_to_protocol_at_exposed_dir::<fdd::DriverDevelopmentMarker>()?;

    let info = get_driver_info(&driver_dev, &mut std::iter::empty()).await?;
    assert!(info
        .iter()
        .any(|d| d.url == Some("fuchsia-boot:///#driver/test-parent-sys.so".to_string())));
    assert!(info.iter().any(|d| d.url == Some("fuchsia-boot:///#driver/test.so".to_string())));

    Ok(())
}

// Manually open our /pkg directory and pass it to DriverTestRealm to see that it works.
#[fasync::run_singlethreaded(test)]
async fn test_pkg_dir() -> Result<()> {
    let realm = RealmBuilder::new().await?;
    realm.driver_test_realm_setup().await?;

    let instance = realm.build().await?;

    let (pkg, pkg_server) =
        fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()?;
    let pkg_flags = io_util::OPEN_RIGHT_READABLE
        | io_util::OPEN_RIGHT_EXECUTABLE
        | fidl_fuchsia_io::OPEN_FLAG_DIRECTORY;
    io_util::connect_in_namespace("/pkg", pkg_server.into_channel(), pkg_flags).unwrap();
    let args = fdt::RealmArgs { boot: Some(pkg), ..fdt::RealmArgs::EMPTY };

    instance.driver_test_realm_start(args).await?;

    let driver_dev =
        instance.root.connect_to_protocol_at_exposed_dir::<fdd::DriverDevelopmentMarker>()?;

    let info = get_driver_info(&driver_dev, &mut std::iter::empty()).await?;
    assert!(info
        .iter()
        .any(|d| d.url == Some("fuchsia-boot:///#driver/test-parent-sys.so".to_string())));
    assert!(info.iter().any(|d| d.url == Some("fuchsia-boot:///#driver/test.so".to_string())));

    let dev = instance.driver_test_realm_connect_to_dev()?;
    device_watcher::recursive_wait_and_open_node(&dev, "sys/test/test").await?;

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_root_driver() -> Result<()> {
    let realm = RealmBuilder::new().await?;
    realm.driver_test_realm_setup().await?;

    let instance = realm.build().await?;
    let args = fdt::RealmArgs {
        root_driver: Some("fuchsia-boot:///#driver/platform-bus.so".to_string()),
        ..fdt::RealmArgs::EMPTY
    };

    instance.driver_test_realm_start(args).await?;

    let dev = instance.driver_test_realm_connect_to_dev()?;
    device_watcher::recursive_wait_and_open_node(&dev, "sys/platform").await?;

    Ok(())
}
