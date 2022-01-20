// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    fidl_fuchsia_driver_test as fdt, fidl_fuchsia_services_test as ft,
    fuchsia_async::{self as fasync, DurationExt, Timer},
    fuchsia_component::client,
    fuchsia_component_test::new::{Capability, RealmBuilder, Ref, Route},
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
    fuchsia_zircon::DurationNum,
};

#[fasync::run_singlethreaded(test)]
async fn test_services() -> Result<()> {
    // Create the RealmBuilder.
    let builder = RealmBuilder::new().await?;
    builder.driver_test_realm_manifest_setup("#meta/realm.cm").await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::service::<ft::DeviceMarker>())
                .from(Ref::child("driver_test_realm"))
                .to(Ref::parent()),
        )
        .await?;
    // Build the Realm.
    let realm = builder.build().await?;
    // Start the DriverTestRealm.
    let args = fdt::RealmArgs {
        root_driver: Some("#meta/root.cm".to_string()),
        use_driver_framework_v2: Some(true),
        ..fdt::RealmArgs::EMPTY
    };
    realm.driver_test_realm_start(args).await?;

    // Find an instance of the `Device` service.
    let instance;
    let service = client::open_service_at_dir::<ft::DeviceMarker>(realm.root.get_exposed_dir())
        .context("Failed to open service")?;
    loop {
        // TODO(fxbug.dev/4776): Once component manager supports watching for
        // service instances, this loop shousld be replaced by a watcher.
        let entries =
            files_async::readdir(&service).await.context("Failed to read service instances")?;
        if let Some(entry) = entries.iter().next() {
            instance = entry.name.clone();
            break;
        }
        Timer::new(100.millis().after_now()).await;
    }

    // Connect to the `Device` service.
    let device = client::connect_to_service_instance_at_dir::<ft::DeviceMarker>(
        realm.root.get_exposed_dir(),
        &instance,
    )
    .context("Failed to open service")?;
    // Use the `ControlPlane` protocol from the `Device` service.
    let control = device.control()?;
    control.control_do().await?;
    // Use the `DataPlane` protocol from the `Device` service.
    let data = device.data()?;
    data.data_do().await?;

    Ok(())
}
