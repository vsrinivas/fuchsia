// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_driver_test as fdt,
    fuchsia_component_test::RealmBuilder,
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
};

async fn do_test(dfv2: bool) -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;
    builder.driver_test_realm_setup().await?;

    let instance = builder.build().await?;
    let root_driver = match dfv2 {
        true => "fuchsia-boot:///#meta/test-parent-sys.cm",
        false => "fuchsia-boot:///#driver/test-parent-sys.so",
    };
    instance
        .driver_test_realm_start(fdt::RealmArgs {
            use_driver_framework_v2: Some(dfv2),
            root_driver: Some(root_driver.to_string()),
            ..fdt::RealmArgs::EMPTY
        })
        .await?;

    let dev = instance.driver_test_realm_connect_to_dev()?;
    println!("dfv2: {}, wait for grandparent", dfv2);
    let _node =
        device_watcher::recursive_wait_and_open_node(&dev, "sys/test/topology-grandparent").await;
    println!("dfv2: {}, wait for child 1", dfv2);
    let _node = device_watcher::recursive_wait_and_open_node(
        &dev,
        "sys/test/topology-grandparent/parent1/child",
    )
    .await;
    println!("dfv2: {}, wait for child 2", dfv2);
    let _node = device_watcher::recursive_wait_and_open_node(
        &dev,
        "sys/test/topology-grandparent/parent2/child",
    )
    .await;
    println!("dfv2: {}, all done!", dfv2);

    Ok(())
}

#[fuchsia::test]
async fn toplogy_test() -> Result<(), Error> {
    do_test(false).await
}

#[fuchsia::test]
async fn toplogy_test_dfv2() -> Result<(), Error> {
    do_test(true).await
}
