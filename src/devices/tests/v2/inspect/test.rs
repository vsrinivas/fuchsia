// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    anyhow::Error,
    anyhow::Result,
    diagnostics_reader::{assert_data_tree, ArchiveReader, DiagnosticsHierarchy, Inspect},
    fidl::endpoints::Proxy,
    fidl_fuchsia_driver_test as fdt, fuchsia_async as fasync,
    fuchsia_component_test::RealmBuilder,
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
};

async fn get_inspect_hierarchy(moniker: String) -> Result<DiagnosticsHierarchy, Error> {
    ArchiveReader::new()
        .add_selector(format!("{}:root", moniker))
        .snapshot::<Inspect>()
        .await?
        .into_iter()
        .next()
        .and_then(|result| result.payload)
        .ok_or(format_err!("expected one inspect hierarchy"))
}

#[fasync::run_singlethreaded(test)]
async fn test_driver_inspect() -> Result<()> {
    // Create the RealmBuilder.
    let builder = RealmBuilder::new().await?;
    builder.driver_test_realm_setup().await?;
    // Build the Realm.
    let instance = builder.build().await?;
    // Start DriverTestRealm
    let args = fdt::RealmArgs {
        root_driver: Some("fuchsia-boot:///#meta/test-parent-sys.cm".to_string()),
        use_driver_framework_v2: Some(true),
        ..fdt::RealmArgs::EMPTY
    };
    instance.driver_test_realm_start(args).await?;

    // Connect to our driver.
    let dev = instance.driver_test_realm_connect_to_dev()?;
    let node = device_watcher::recursive_wait_and_open_node(&dev, "sys/test/root-driver").await?;

    let moniker = format!(
        "realm_builder\\:{}/driver_test_realm/boot-drivers\\:root.sys.test",
        instance.root.child_name()
    );
    // Check the inspect metrics.
    let mut hierarchy = get_inspect_hierarchy(moniker.clone()).await?;
    assert_data_tree!(hierarchy, root: contains {
        connection_info: contains {
            request_count: 0u64,
        }
    });

    // Do the request and check the inspect metrics again.
    let driver = fidl_fuchsia_inspect_test::HandshakeProxy::new(node.into_channel().unwrap());
    driver.r#do().await.unwrap();

    hierarchy = get_inspect_hierarchy(moniker).await?;
    assert_data_tree!(hierarchy, root: contains {
        connection_info: contains {
            request_count: 1u64,
        }
    });

    Ok(())
}
