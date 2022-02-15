// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod test_utils;

use {
    anyhow::{Context, Result},
    fidl_fuchsia_driver_development as fdd, fidl_fuchsia_driver_framework as fdf,
    fidl_fuchsia_driver_test as fdt,
    fuchsia_async::{
        self as fasync,
        futures::{stream::FuturesUnordered, TryStreamExt},
    },
    fuchsia_component_test::new::{RealmBuilder, RealmInstance},
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
    fuchsia_zircon_status as zx_status,
    std::iter::FromIterator,
};

const DRIVERS_TO_WAIT_ON: [&str; 1] = ["sys/test/sample_driver"];

const NO_PROTOCOL_DFV2_NODE_PROPERTY_LIST: Option<[fdf::NodeProperty; 1]> =
    Some([fdf::NodeProperty {
        key: Some(fdf::NodePropertyKey::IntValue(bind::ddk_bind_constants::BIND_PROTOCOL)),
        value: Some(fdf::NodePropertyValue::IntValue(0)),
        unknown_data: None,
        ..fdf::NodeProperty::EMPTY
    }]);

const TEST_PARENT_DFV2_NODE_PROPERTY_LIST: Option<[fdf::NodeProperty; 1]> =
    Some([fdf::NodeProperty {
        key: Some(fdf::NodePropertyKey::IntValue(bind::ddk_bind_constants::BIND_PROTOCOL)),
        value: Some(fdf::NodePropertyValue::IntValue(fidl_bind_fuchsia_test::BIND_PROTOCOL_PARENT)),
        unknown_data: None,
        ..fdf::NodeProperty::EMPTY
    }]);

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

fn send_get_device_info_request(
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
    let iterator = send_get_device_info_request(service, device_filter)?;

    let mut device_infos = Vec::new();
    loop {
        let mut device_info =
            iterator.get_next().await.context("FIDL call to get device info failed")?;
        if device_info.len() == 0 {
            break;
        }
        device_infos.append(&mut device_info);
    }
    Ok(device_infos)
}

fn send_get_driver_info_request(
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
    let iterator = send_get_driver_info_request(service, driver_filter)?;

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
    const ROOT_DRIVER_DFV2_URL: &str = "fuchsia-boot:///#meta/test-parent-sys.cm";

    let builder = RealmBuilder::new().await?;
    builder.driver_test_realm_setup().await?;
    let instance = builder.build().await?;

    let mut realm_args = fdt::RealmArgs::EMPTY;
    realm_args.use_driver_framework_v2 = Some(use_dfv2);
    if use_dfv2 {
        // DriverTestRealm attempts to bind the .so of test-parent-sys if not explicitly requested otherwise.
        realm_args.root_driver = Some(ROOT_DRIVER_DFV2_URL.to_owned());
    }
    instance.driver_test_realm_start(realm_args).await?;

    let driver_dev =
        instance.root.connect_to_protocol_at_exposed_dir::<fdd::DriverDevelopmentMarker>()?;

    Ok((instance, driver_dev))
}

fn assert_contains_driver_url(driver_infos: &Vec<fdd::DriverInfo>, expected_driver_url: &str) {
    assert!(driver_infos
        .iter()
        .find(|driver_info| driver_info.url.as_ref().expect("Missing device URL")
            == expected_driver_url)
        .is_some());
}

// GetDriverInfo tests
// DFv1
#[fasync::run_singlethreaded(test)]
async fn test_get_driver_info_no_filter_dfv1() -> Result<()> {
    let (instance, driver_dev) = set_up_test_driver_realm(false).await?;
    // TODO(fxbug.dev/92702): Remove call to wait for drivers once issue is fixed.
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let driver_infos = get_driver_info(&driver_dev, &[]).await?;

    assert_eq!(driver_infos.len(), 2);
    assert_contains_driver_url(&driver_infos, "fuchsia-boot:///#driver/sample-driver.so");
    assert_contains_driver_url(&driver_infos, "fuchsia-boot:///#driver/test-parent-sys.so");
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_driver_info_with_filter_dfv1() -> Result<()> {
    const DRIVER_FILTER: [&str; 1] = ["fuchsia-boot:///#driver/sample-driver.so"];

    let (instance, driver_dev) = set_up_test_driver_realm(false).await?;
    // TODO(fxbug.dev/92702): Remove call to wait for drivers once issue is fixed.
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let driver_infos = get_driver_info(&driver_dev, &DRIVER_FILTER).await?;

    assert_eq!(driver_infos.len(), 1);
    assert_contains_driver_url(&driver_infos, "fuchsia-boot:///#driver/sample-driver.so");
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_driver_info_with_mixed_filter_dfv1() -> Result<()> {
    const DRIVER_FILTER: [&str; 2] = ["fuchsia-boot:///#driver/sample-driver.so", "foo"];

    let (instance, driver_dev) = set_up_test_driver_realm(false).await?;
    // TODO(fxbug.dev/92702): Remove call to wait for drivers once issue is fixed.
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let iterator = send_get_driver_info_request(&driver_dev, &DRIVER_FILTER)?;
    let res = iterator.get_next().await.expect_err("A driver should not be returned");

    assert_not_found_error(res);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_driver_info_with_duplicate_filter_dfv1() -> Result<()> {
    const DRIVER_FILTER: [&str; 2] =
        ["fuchsia-boot:///#driver/sample-driver.so", "fuchsia-boot:///#driver/sample-driver.so"];

    let (instance, driver_dev) = set_up_test_driver_realm(false).await?;
    // TODO(fxbug.dev/92702): Remove call to wait for drivers once issue is fixed.
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let driver_infos = get_driver_info(&driver_dev, &DRIVER_FILTER).await?;

    assert_eq!(driver_infos.len(), 2);
    assert_eq!(
        driver_infos
            .into_iter()
            .filter(|driver_info| driver_info.url.as_ref().expect("Missing driver URL")
                == "fuchsia-boot:///#driver/sample-driver.so")
            .count(),
        2
    );

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_driver_info_with_incomplete_filter_dfv1() -> Result<()> {
    const DRIVER_FILTER: [&str; 1] = ["fuchsia-boot:///#driver/sample-driver"];

    let (instance, driver_dev) = set_up_test_driver_realm(false).await?;
    // TODO(fxbug.dev/92702): Remove call to wait for drivers once issue is fixed.
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let iterator = send_get_driver_info_request(&driver_dev, &DRIVER_FILTER)?;
    let res = iterator.get_next().await.expect_err("A driver should not be returned");

    assert_not_found_error(res);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_driver_info_not_found_filter_dfv1() -> Result<()> {
    const DRIVER_FILTER: [&str; 1] = ["foo"];

    let (instance, driver_dev) = set_up_test_driver_realm(false).await?;
    // TODO(fxbug.dev/92702): Remove call to wait for drivers once issue is fixed.
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let iterator = send_get_driver_info_request(&driver_dev, &DRIVER_FILTER)?;
    let res = iterator.get_next().await.expect_err("A driver should not be returned");

    assert_not_found_error(res);
    Ok(())
}

// DFv2
#[fasync::run_singlethreaded(test)]
async fn test_get_driver_info_no_filter_dfv2() -> Result<()> {
    let (instance, driver_dev) = set_up_test_driver_realm(true).await?;
    // TODO(fxbug.dev/92702): Remove call to wait for drivers once issue is fixed.
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let driver_infos = get_driver_info(&driver_dev, &[]).await?;

    assert_eq!(driver_infos.len(), 2);
    assert_contains_driver_url(&driver_infos, "fuchsia-boot:///#meta/sample-driver.cm");
    assert_contains_driver_url(&driver_infos, "fuchsia-boot:///#meta/test-parent-sys.cm");
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_driver_info_with_filter_dfv2() -> Result<()> {
    const DRIVER_FILTER: [&str; 1] = ["fuchsia-boot:///#meta/sample-driver.cm"];

    let (instance, driver_dev) = set_up_test_driver_realm(true).await?;
    // TODO(fxbug.dev/92702): Remove call to wait for drivers once issue is fixed.
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let driver_infos = get_driver_info(&driver_dev, &DRIVER_FILTER).await?;

    assert_eq!(driver_infos.len(), 1);
    assert_contains_driver_url(&driver_infos, "fuchsia-boot:///#meta/sample-driver.cm");
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_driver_info_with_duplicate_filter_dfv2() -> Result<()> {
    const DRIVER_FILTER: [&str; 2] =
        ["fuchsia-boot:///#meta/sample-driver.cm", "fuchsia-boot:///#meta/sample-driver.cm"];

    let (instance, driver_dev) = set_up_test_driver_realm(true).await?;
    // TODO(fxbug.dev/92702): Remove call to wait for drivers once issue is fixed.
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let driver_infos = get_driver_info(&driver_dev, &DRIVER_FILTER).await?;

    assert_eq!(driver_infos.len(), 1);
    assert_contains_driver_url(&driver_infos, "fuchsia-boot:///#meta/sample-driver.cm");

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_driver_info_with_mixed_filter_dfv2() -> Result<()> {
    const DRIVER_FILTER: [&str; 2] = ["fuchsia-boot:///#meta/sample-driver.cm", "foo"];

    let (instance, driver_dev) = set_up_test_driver_realm(true).await?;
    // TODO(fxbug.dev/92702): Remove call to wait for drivers once issue is fixed.
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let driver_infos = get_driver_info(&driver_dev, &DRIVER_FILTER).await?;

    assert_eq!(driver_infos.len(), 1);
    assert_contains_driver_url(&driver_infos, "fuchsia-boot:///#meta/sample-driver.cm");

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_driver_info_with_incomplete_filter_dfv2() -> Result<()> {
    const DRIVER_FILTER: [&str; 1] = ["fuchsia-boot:///#meta/sample-driver"];

    let (instance, driver_dev) = set_up_test_driver_realm(true).await?;
    // TODO(fxbug.dev/92702): Remove call to wait for drivers once issue is fixed.
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let driver_infos = get_driver_info(&driver_dev, &DRIVER_FILTER).await?;

    assert!(driver_infos.is_empty());
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_driver_info_not_found_filter_dfv2() -> Result<()> {
    const DRIVER_FILTER: [&str; 1] = ["foo"];

    let (instance, driver_dev) = set_up_test_driver_realm(true).await?;
    // TODO(fxbug.dev/92702): Remove call to wait for drivers once issue is fixed.
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let driver_infos = get_driver_info(&driver_dev, &DRIVER_FILTER).await?;

    assert!(driver_infos.is_empty());
    Ok(())
}

// GetDeviceInfo tests
// DFv1
#[fasync::run_singlethreaded(test)]
async fn test_get_device_info_no_filter_dfv1() -> Result<()> {
    let (instance, driver_dev) = set_up_test_driver_realm(false).await?;
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let device_infos = get_device_info(&driver_dev, &[]).await?;

    let device_nodes = test_utils::create_device_topology(device_infos);
    assert_eq!(device_nodes.len(), 1);

    let root_sys_test = &device_nodes[0];
    assert_eq!(
        root_sys_test.info.topological_path.as_ref().expect("DFv1 device missing topological path"),
        "/dev/sys/test"
    );
    assert!(root_sys_test.info.moniker.is_none());
    assert!(root_sys_test.info.node_property_list.is_none());
    assert_eq!(
        root_sys_test
            .info
            .bound_driver_libname
            .as_ref()
            .expect("DFv1 driver missing bound driver libname"),
        "fuchsia-boot:///#driver/test-parent-sys.so"
    );
    assert_eq!(root_sys_test.num_children, 1);
    assert_eq!(root_sys_test.child_nodes.len(), 1);

    let sample_driver = &root_sys_test.child_nodes[0];
    assert_eq!(
        sample_driver.info.topological_path.as_ref().expect("DFv1 device missing topological path"),
        "/dev/sys/test/sample_driver"
    );
    assert!(sample_driver.info.moniker.is_none());
    assert!(sample_driver.info.node_property_list.is_none());
    assert_eq!(
        sample_driver
            .info
            .bound_driver_libname
            .as_ref()
            .expect("DFv1 driver missing bound driver libname"),
        "fuchsia-boot:///#driver/sample-driver.so"
    );
    assert_eq!(sample_driver.num_children, 0);
    assert!(sample_driver.child_nodes.is_empty());
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_device_info_with_filter_dfv1() -> Result<()> {
    const DEVICE_FILTER: [&str; 1] = ["sys/test"];

    let (instance, driver_dev) = set_up_test_driver_realm(false).await?;
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let device_infos = get_device_info(&driver_dev, &DEVICE_FILTER).await?;

    let device_nodes = test_utils::create_device_topology(device_infos);
    assert_eq!(device_nodes.len(), 1);

    let sys_test = &device_nodes[0];
    assert_eq!(
        sys_test.info.topological_path.as_ref().expect("DFv1 device missing topological path"),
        "/dev/sys/test"
    );
    assert!(sys_test.info.moniker.is_none());
    assert!(sys_test.info.node_property_list.is_none());
    assert_eq!(
        sys_test
            .info
            .bound_driver_libname
            .as_ref()
            .expect("DFv1 driver missing bound driver libname"),
        "fuchsia-boot:///#driver/test-parent-sys.so"
    );
    assert_eq!(sys_test.num_children, 1);
    assert!(sys_test.child_nodes.is_empty());
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_device_info_with_duplicate_filter_dfv1() -> Result<()> {
    const DEVICE_FILTER: [&str; 2] = ["sys/test/sample_driver", "sys/test/sample_driver"];

    let (instance, driver_dev) = set_up_test_driver_realm(false).await?;
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let device_infos = get_device_info(&driver_dev, &DEVICE_FILTER).await?;

    let device_nodes = test_utils::create_device_topology(device_infos);
    assert_eq!(device_nodes.len(), 2);

    let sample_driver = &device_nodes[0];
    assert_eq!(
        sample_driver.info.topological_path.as_ref().expect("DFv1 device missing topological path"),
        "/dev/sys/test/sample_driver"
    );
    assert!(sample_driver.info.moniker.is_none());
    assert!(sample_driver.info.node_property_list.is_none());
    assert_eq!(
        sample_driver
            .info
            .bound_driver_libname
            .as_ref()
            .expect("DFv1 driver missing bound driver libname"),
        "fuchsia-boot:///#driver/sample-driver.so"
    );
    assert!(sample_driver.child_nodes.is_empty());

    let sample_driver = &device_nodes[1];
    assert_eq!(
        sample_driver.info.topological_path.as_ref().expect("DFv1 device missing topological path"),
        "/dev/sys/test/sample_driver"
    );
    assert!(sample_driver.info.moniker.is_none());
    assert!(sample_driver.info.node_property_list.is_none());
    assert_eq!(
        sample_driver
            .info
            .bound_driver_libname
            .as_ref()
            .expect("DFv1 driver missing bound driver libname"),
        "fuchsia-boot:///#driver/sample-driver.so"
    );
    assert_eq!(sample_driver.num_children, 0);
    assert!(sample_driver.child_nodes.is_empty());
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_device_info_with_incomplete_filter_dfv1() -> Result<()> {
    const DEVICE_FILTER: [&str; 1] = ["sys/test/sample"];

    let (instance, driver_dev) = set_up_test_driver_realm(false).await?;
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let iterator = send_get_device_info_request(&driver_dev, &DEVICE_FILTER)?;
    let res = iterator.get_next().await.expect_err("A device should not be returned");

    assert_not_found_error(res);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_device_info_not_found_filter_dfv1() -> Result<()> {
    const DEVICE_FILTER: [&str; 1] = ["foo"];

    let (instance, driver_dev) = set_up_test_driver_realm(false).await?;
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let iterator = send_get_device_info_request(&driver_dev, &DEVICE_FILTER)?;
    let res = iterator.get_next().await.expect_err("A device should not be returned");

    assert_not_found_error(res);
    Ok(())
}

// DFv2
#[fasync::run_singlethreaded(test)]
async fn test_get_device_info_no_filter_dfv2() -> Result<()> {
    let (instance, driver_dev) = set_up_test_driver_realm(true).await?;
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let device_infos = get_device_info(&driver_dev, &[]).await?;

    let device_nodes = test_utils::create_device_topology(device_infos);
    assert_eq!(device_nodes.len(), 1);

    let root = &device_nodes[0];
    assert_eq!(root.info.moniker.as_ref().expect("DFv2 node missing moniker"), "root");
    assert!(root.info.bound_driver_libname.is_none(), "DFv2 node specified bound driver libname");
    assert_eq!(
        root.info.bound_driver_url.as_ref().expect("DFv2 node missing driver URL"),
        "fuchsia-boot:///#meta/test-parent-sys.cm"
    );
    assert!(root.info.property_list.is_none());
    assert!(root.info.node_property_list.is_none());
    assert_eq!(root.num_children, 1);
    assert_eq!(root.child_nodes.len(), 1);

    let sys = &root.child_nodes[0];
    assert_eq!(sys.info.moniker.as_ref().expect("DFv2 node missing moniker"), "root.sys");
    assert!(sys.info.bound_driver_libname.is_none(), "DFv2 node specified bound driver libname");
    assert!(sys.info.bound_driver_url.is_none());
    assert_eq!(
        sys.info.node_property_list.as_ref().map(|x| x.as_slice()),
        NO_PROTOCOL_DFV2_NODE_PROPERTY_LIST.as_ref().map(|x| x.as_slice())
    );
    assert_eq!(sys.num_children, 1);
    assert_eq!(sys.child_nodes.len(), 1);

    let test = &sys.child_nodes[0];
    assert_eq!(test.info.moniker.as_ref().expect("DFv2 node missing moniker"), "root.sys.test");
    assert!(test.info.bound_driver_libname.is_none(), "DFv2 node specified bound driver libname");
    assert_eq!(
        test.info.bound_driver_url.as_ref().expect("DFv2 node missing driver URL"),
        "fuchsia-boot:///#meta/sample-driver.cm"
    );
    assert_eq!(
        test.info.node_property_list.as_ref().map(|x| x.as_slice()),
        TEST_PARENT_DFV2_NODE_PROPERTY_LIST.as_ref().map(|x| x.as_slice())
    );
    assert_eq!(test.num_children, 1);
    assert_eq!(test.child_nodes.len(), 1);

    let sample_driver = &test.child_nodes[0];
    println!("{:?}", sample_driver.info.node_property_list);
    assert_eq!(
        sample_driver.info.moniker.as_ref().expect("DFv2 node missing moniker"),
        "root.sys.test.sample_driver"
    );
    assert!(
        sample_driver.info.bound_driver_libname.is_none(),
        "DFv2 node specified bound driver libname"
    );
    assert!(sample_driver.info.bound_driver_url.is_none());
    assert_eq!(
        sample_driver.info.node_property_list.as_ref().map(|x| x.as_slice()),
        NO_PROTOCOL_DFV2_NODE_PROPERTY_LIST.as_ref().map(|x| x.as_slice())
    );
    assert_eq!(sample_driver.num_children, 0);
    assert!(sample_driver.child_nodes.is_empty());
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_device_info_with_filter_dfv2() -> Result<()> {
    const DEVICE_FILTER: [&str; 1] = ["root.sys.test"];

    let (instance, driver_dev) = set_up_test_driver_realm(true).await?;
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let device_infos = get_device_info(&driver_dev, &DEVICE_FILTER).await?;

    let device_nodes = test_utils::create_device_topology(device_infos);
    assert_eq!(device_nodes.len(), 1);

    let root_sys_test = &device_nodes[0];
    assert_eq!(
        root_sys_test.info.moniker.as_ref().expect("DFv2 node missing moniker"),
        "root.sys.test"
    );
    assert!(
        root_sys_test.info.bound_driver_libname.is_none(),
        "DFv2 node specified bound driver libname"
    );
    assert_eq!(
        root_sys_test.info.bound_driver_url.as_ref().expect("DFv2 node missing driver URL"),
        "fuchsia-boot:///#meta/sample-driver.cm"
    );
    assert_eq!(
        root_sys_test.info.node_property_list.as_ref().map(|x| x.as_slice()),
        TEST_PARENT_DFV2_NODE_PROPERTY_LIST.as_ref().map(|x| x.as_slice())
    );
    assert!(root_sys_test.child_nodes.is_empty());
    assert_eq!(root_sys_test.num_children, 1);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_device_info_with_duplicate_filter_dfv2() -> Result<()> {
    const DEVICE_FILTER: [&str; 2] = ["root.sys.test", "root.sys.test"];

    let (instance, driver_dev) = set_up_test_driver_realm(true).await?;
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let device_infos = get_device_info(&driver_dev, &DEVICE_FILTER).await?;

    let device_nodes = test_utils::create_device_topology(device_infos);
    assert_eq!(device_nodes.len(), 1);

    let root_sys_test = &device_nodes[0];
    assert_eq!(
        root_sys_test.info.moniker.as_ref().expect("DFv2 node missing moniker"),
        "root.sys.test"
    );
    assert!(
        root_sys_test.info.bound_driver_libname.is_none(),
        "DFv2 node specified bound driver libname"
    );
    assert_eq!(
        root_sys_test.info.bound_driver_url.as_ref().expect("DFv2 node missing driver URL"),
        "fuchsia-boot:///#meta/sample-driver.cm"
    );
    assert_eq!(
        root_sys_test.info.node_property_list.as_ref().map(|x| x.as_slice()),
        TEST_PARENT_DFV2_NODE_PROPERTY_LIST.as_ref().map(|x| x.as_slice())
    );
    assert!(root_sys_test.child_nodes.is_empty());
    assert_eq!(root_sys_test.num_children, 1);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_device_info_with_incomplete_filter_dfv2() -> Result<()> {
    const DEVICE_FILTER: [&str; 1] = ["root.sys.te"];

    let (instance, driver_dev) = set_up_test_driver_realm(true).await?;
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let device_infos = get_device_info(&driver_dev, &DEVICE_FILTER).await?;

    assert!(device_infos.is_empty());
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_device_info_not_found_filter_dfv2() -> Result<()> {
    const DEVICE_FILTER: [&str; 1] = ["foo"];

    let (instance, driver_dev) = set_up_test_driver_realm(true).await?;
    wait_for_drivers(&instance, &DRIVERS_TO_WAIT_ON).await?;
    let device_infos = get_device_info(&driver_dev, &DEVICE_FILTER).await?;

    assert!(device_infos.is_empty());
    Ok(())
}
