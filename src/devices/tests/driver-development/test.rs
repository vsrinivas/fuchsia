// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    fidl_fuchsia_driver_development as fdd, fidl_fuchsia_driver_test as fdt,
    fuchsia_async::{
        self as fasync,
        futures::{stream::FuturesUnordered, TryStreamExt},
    },
    fuchsia_component_test::new::{RealmBuilder, RealmInstance},
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
    fuchsia_zircon_status as zx_status,
    lazy_static::lazy_static,
    std::{
        collections::{HashMap, HashSet},
        iter::FromIterator,
    },
};

const DRIVER_FILTER: [&str; 1] = ["fuchsia-boot:///#driver/sample-driver.so"];
const NOT_FOUND_DRIVER_FILTER: [&str; 1] = ["foo"];
const DEVICE_FILTER: [&str; 1] = ["sys/test/sample_driver"];
const NOT_FOUND_DEVICE_FILTER: [&str; 1] = ["foo"];
const DRIVERS_TO_WAIT_ON: [&str; 1] = ["sys/test/sample_driver"];

lazy_static! {
  static ref EXPECTED_DRIVER_URLS_NO_FILTER: HashSet<String> = {
    let mut set = HashSet::with_capacity(2);
    set.insert("fuchsia-boot:///#driver/sample-driver.so".to_owned());
    set.insert("fuchsia-boot:///#driver/test-parent-sys.so".to_owned());
    set
  };

  static ref EXPECTED_DRIVER_URLS_WITH_FILTER: HashSet<String> = {
    let mut set = HashSet::with_capacity(2);
    set.insert("fuchsia-boot:///#driver/sample-driver.so".to_owned());
    set
  };

  // Key: topological path
  // Value: bound driver libname
  static ref EXPECTED_DEVICE_INFO_NO_FILTER: HashMap<String, String> = {
    let mut map = HashMap::with_capacity(2);
    map.insert("/dev/sys/test".to_owned(), "fuchsia-boot:///#driver/test-parent-sys.so".to_owned());
    map.insert("/dev/sys/test/sample_driver".to_owned(), "fuchsia-boot:///#driver/sample-driver.so".to_owned());
    map
  };

  static ref EXPECTED_DEVICE_INFO_WITH_FILTER: HashMap<String, String> = {
    let mut map = HashMap::with_capacity(2);
    map.insert("/dev/sys/test/sample_driver".to_owned(), "fuchsia-boot:///#driver/sample-driver.so".to_owned());
    map
  };
}

fn assert_not_found_error(error: fidl::Error) {
    if let fidl::Error::ClientChannelClosed { status, protocol_name: _ } = error {
        assert_eq!(status, zx_status::Status::NOT_FOUND);
    } else {
        panic!("Expcted ClientChannelClosed error");
    }
}

async fn wait_for_drivers(instance: &RealmInstance, driver_paths: &[&str]) -> Result<()> {
    let dev = instance.driver_test_realm_connect_to_dev()?;
    let mut tasks = FuturesUnordered::from_iter(
        driver_paths
            .iter()
            .map(|driver_path| device_watcher::recursive_wait_and_open_node(&dev, driver_path)),
    );
    while let Some(_) = tasks.try_next().await? {}
    Ok(())
}

async fn send_get_device_info_request(
    service: &fdd::DriverDevelopmentProxy,
    device_filter: &[&str],
) -> Result<fdd::DeviceInfoIteratorProxy> {
    let (iterator, iterator_server) =
        fidl::endpoints::create_proxy::<fdd::DeviceInfoIteratorMarker>()?;

    service
        .get_device_info(&mut device_filter.iter().map(|i| *i), iterator_server)
        .context("FIDL call to get device info failed")?;

    Ok(iterator)
}

async fn get_device_info(
    service: &fdd::DriverDevelopmentProxy,
    device_filter: &[&str],
) -> Result<Vec<fdd::DeviceInfo>> {
    let iterator = send_get_device_info_request(service, device_filter).await?;

    let mut device_infos = Vec::new();
    loop {
        let mut device_info =
            iterator.get_next().await.context("FIDL call to get device info failed")?;
        if device_info.len() == 0 {
            break;
        }
        device_infos.append(&mut device_info)
    }
    Ok(device_infos)
}

async fn send_get_driver_info_request(
    service: &fdd::DriverDevelopmentProxy,
    driver_filter: &[&str],
) -> Result<fdd::DriverInfoIteratorProxy> {
    let (iterator, iterator_server) =
        fidl::endpoints::create_proxy::<fdd::DriverInfoIteratorMarker>()?;

    service
        .get_driver_info(&mut driver_filter.iter().map(|i| *i), iterator_server)
        .context("FIDL call to get driver info failed")?;

    Ok(iterator)
}

async fn get_driver_info(
    service: &fdd::DriverDevelopmentProxy,
    driver_filter: &[&str],
) -> Result<Vec<fdd::DriverInfo>> {
    let iterator = send_get_driver_info_request(service, driver_filter).await?;

    let mut driver_infos = Vec::new();
    loop {
        let mut driver_info =
            iterator.get_next().await.context("FIDL call to get driver info failed")?;
        if driver_info.len() == 0 {
            break;
        }
        driver_infos.append(&mut driver_info)
    }
    Ok(driver_infos)
}

async fn set_up_test_driver_realm(
    use_dfv2: bool,
) -> Result<(RealmInstance, fdd::DriverDevelopmentProxy)> {
    let builder = RealmBuilder::new().await?;
    builder.driver_test_realm_setup().await?;
    let instance = builder.build().await?;

    let mut realm_args = fdt::RealmArgs::EMPTY;
    realm_args.use_driver_framework_v2 = Some(use_dfv2);
    instance.driver_test_realm_start(realm_args).await?;

    let driver_dev =
        instance.root.connect_to_protocol_at_exposed_dir::<fdd::DriverDevelopmentMarker>()?;

    Ok((instance, driver_dev))
}

fn assert_driver_urls(driver_infos: Vec<fdd::DriverInfo>, expected_driver_urls: &HashSet<String>) {
    let actual: HashSet<String> =
        driver_infos.into_iter().map(|info| info.url.expect("Driver URL missing")).collect();
    assert_eq!(actual, *expected_driver_urls);
}

fn assert_device_infos(
    device_infos: Vec<fdd::DeviceInfo>,
    expected_device_info: &HashMap<String, String>,
) {
    let actual: HashMap<String, String> = device_infos
        .into_iter()
        .map(|device_info| {
            let topological_path =
                device_info.topological_path.expect("Device topologial path missing");
            let bound_driver_libname =
                device_info.bound_driver_libname.expect("Device bound driver libname missing");
            (topological_path, bound_driver_libname)
        })
        .collect();
    assert_eq!(actual, *expected_device_info);
}

// GetDriverInfo tests
// DFv1
#[fasync::run_singlethreaded(test)]
async fn test_get_driver_info_no_filter_vf1() -> Result<()> {
    let (_instance, driver_dev) = set_up_test_driver_realm(false).await?;
    let driver_infos = get_driver_info(&driver_dev, &[]).await?;
    assert_driver_urls(driver_infos, &EXPECTED_DRIVER_URLS_NO_FILTER);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_driver_info_filter_vf1() -> Result<()> {
    let (_instance, driver_dev) = set_up_test_driver_realm(false).await?;
    let driver_infos = get_driver_info(&driver_dev, &DRIVER_FILTER).await?;
    assert_driver_urls(driver_infos, &EXPECTED_DRIVER_URLS_WITH_FILTER);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_driver_info_not_found_filter_vf1() -> Result<()> {
    let (_instance, driver_dev) = set_up_test_driver_realm(false).await?;
    let iterator = send_get_driver_info_request(&driver_dev, &NOT_FOUND_DRIVER_FILTER).await?;
    let res = iterator.get_next().await.expect_err("A driver should not be returned");
    assert_not_found_error(res);
    Ok(())
}

// DFv2
// TODO(fxbug.dev/90735): Add GetDriverInfo tests using DFv2 once GetDriverInfo is implemented in DFv2.

// GetDeviceInfo tests
// DFv1
#[fasync::run_singlethreaded(test)]
async fn test_get_device_info_no_filter_vf1() -> Result<()> {
    let (instance, driver_dev) = set_up_test_driver_realm(false).await?;
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let device_infos = get_device_info(&driver_dev, &[]).await?;
    assert_device_infos(device_infos, &EXPECTED_DEVICE_INFO_NO_FILTER);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_device_info_with_filter_vf1() -> Result<()> {
    let (instance, driver_dev) = set_up_test_driver_realm(false).await?;
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let device_infos = get_device_info(&driver_dev, &DEVICE_FILTER).await?;
    assert_device_infos(device_infos, &EXPECTED_DEVICE_INFO_WITH_FILTER);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_device_info_not_found_filter_vf1() -> Result<()> {
    let (instance, driver_dev) = set_up_test_driver_realm(false).await?;
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let iterator = send_get_device_info_request(&driver_dev, &NOT_FOUND_DEVICE_FILTER).await?;
    let res = iterator.get_next().await.expect_err("A device should not be returned");
    assert_not_found_error(res);
    Ok(())
}

// TODO(fxbug.dev/90735): Add GetDeviceInfo tests using DFv2 once GetDeviceInfo is implemented in DFv2.
