// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    fidl_fuchsia_driver_development as fdd, fidl_fuchsia_driver_test as fdt,
    fidl_fuchsia_io2 as fio2, fuchsia_async as fasync,
    fuchsia_component_test::builder::{
        Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint,
    },
};

async fn new_driver_realm_builder() -> Result<RealmBuilder> {
    let mut builder = RealmBuilder::new().await?;
    builder
        .add_component("driver_test_realm", ComponentSource::url("#meta/driver_test_realm.cm"))
        .await?;

    let driver_realm = RouteEndpoint::component("driver_test_realm");
    builder.add_route(CapabilityRoute {
        capability: Capability::protocol("fuchsia.logger.LogSink"),
        source: RouteEndpoint::AboveRoot,
        targets: vec![driver_realm.clone()],
    })?;
    builder.add_route(CapabilityRoute {
        capability: Capability::protocol("fuchsia.process.Launcher"),
        source: RouteEndpoint::AboveRoot,
        targets: vec![driver_realm.clone()],
    })?;
    builder.add_route(CapabilityRoute {
        capability: Capability::protocol("fuchsia.sys.Launcher"),
        source: RouteEndpoint::AboveRoot,
        targets: vec![driver_realm.clone()],
    })?;
    builder.add_route(CapabilityRoute {
        capability: Capability::protocol("fuchsia.driver.development.DriverDevelopment"),
        source: driver_realm.clone(),
        targets: vec![RouteEndpoint::AboveRoot],
    })?;
    builder.add_route(CapabilityRoute {
        capability: Capability::protocol("fuchsia.driver.test.Realm"),
        source: driver_realm.clone(),
        targets: vec![RouteEndpoint::AboveRoot],
    })?;
    builder.add_route(CapabilityRoute {
        capability: Capability::directory("dev", "dev", fio2::RW_STAR_DIR),
        source: driver_realm.clone(),
        targets: vec![RouteEndpoint::AboveRoot],
    })?;
    Ok(builder)
}

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

// Run DriverTestRealm with no arguments and see that the drivers in our package
// are loaded.
#[fasync::run_singlethreaded(test)]
async fn test_empty_args() -> Result<()> {
    let builder = new_driver_realm_builder().await?;
    let realm = builder.build().create().await?;

    let realm_config = realm.root.connect_to_protocol_at_exposed_dir::<fdt::RealmMarker>()?;
    realm_config.start(fdt::RealmArgs::EMPTY).await?.unwrap();

    let driver_dev =
        realm.root.connect_to_protocol_at_exposed_dir::<fdd::DriverDevelopmentMarker>()?;

    let info = get_driver_info(&driver_dev, &mut std::iter::empty()).await?;
    assert!(info.len() == 2);
    assert!(info
        .iter()
        .any(|d| d.url == Some("fuchsia-boot:///#driver/test-parent-sys.so".to_string())));
    assert!(info.iter().any(|d| d.url == Some("fuchsia-boot:///#driver/test.so".to_string())));

    // Connect to /dev and make sure our drivers come up.
    // TODO: If this isn't done, the test will flake with an error because DriverManager
    // will shut down while the drivers are still trying to bind.
    let (dev, dev_server) =
        fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()?;
    realm
        .root
        .connect_request_to_named_protocol_at_exposed_dir("dev", dev_server.into_channel())?;
    let dev = fidl_fuchsia_io::DirectoryProxy::new(fidl::handle::AsyncChannel::from_channel(
        dev.into_channel(),
    )?);
    device_watcher::recursive_wait_and_open_node(&dev, "sys/test/test").await?;
    Ok(())
}

// Manually open our /pkg directory and pass it to DriverTestRealm to see that it works.
#[fasync::run_singlethreaded(test)]
async fn test_pkg_dir() -> Result<()> {
    let builder = new_driver_realm_builder().await?;
    let realm = builder.build().create().await?;
    let realm_config = realm.root.connect_to_protocol_at_exposed_dir::<fdt::RealmMarker>()?;

    let (pkg, pkg_server) =
        fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()?;
    let pkg_flags = io_util::OPEN_RIGHT_READABLE
        | io_util::OPEN_RIGHT_WRITABLE
        | fidl_fuchsia_io::OPEN_FLAG_DIRECTORY;
    io_util::connect_in_namespace("/pkg", pkg_server.into_channel(), pkg_flags).unwrap();
    let args = fdt::RealmArgs { boot: Some(pkg), ..fdt::RealmArgs::EMPTY };

    realm_config.start(args).await?.unwrap();

    let driver_dev =
        realm.root.connect_to_protocol_at_exposed_dir::<fdd::DriverDevelopmentMarker>()?;

    let info = get_driver_info(&driver_dev, &mut std::iter::empty()).await?;
    assert!(info.len() == 2);
    assert!(info
        .iter()
        .any(|d| d.url == Some("fuchsia-boot:///#driver/test-parent-sys.so".to_string())));
    assert!(info.iter().any(|d| d.url == Some("fuchsia-boot:///#driver/test.so".to_string())));

    let (dev, dev_server) =
        fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()?;
    realm
        .root
        .connect_request_to_named_protocol_at_exposed_dir("dev", dev_server.into_channel())?;
    let dev = fidl_fuchsia_io::DirectoryProxy::new(fidl::handle::AsyncChannel::from_channel(
        dev.into_channel(),
    )?);
    device_watcher::recursive_wait_and_open_node(&dev, "sys/test/test").await?;

    Ok(())
}
