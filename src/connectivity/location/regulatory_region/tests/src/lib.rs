// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_location_namedplace::{
        RegulatoryRegionConfiguratorMarker, RegulatoryRegionConfiguratorProxy as ConfigProxy,
        RegulatoryRegionWatcherMarker, RegulatoryRegionWatcherProxy as WatcherProxy,
    },
    fidl_fuchsia_sys::LauncherProxy,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{launch, launcher, App},
        fuchsia_single_component_package_url,
    },
};

const COMPONENT_URL: &str = fuchsia_single_component_package_url!("regulatory_region");

#[fasync::run_singlethreaded(test)]
async fn from_none_state_sending_get_then_set_yields_expected_region() -> Result<(), Error> {
    // Set up handles.
    let test_context = new_test_context()?;
    let (configurator, watcher) = (&test_context.configurator, &test_context.watcher);

    // **Caution**
    //
    // * Because `get_update()` and `set_region()` are sent on separate channels, we don't know the
    //   order in which they'll arrive at the service.
    // * Additionally, we don't have any guarantees about the order in which the service will
    //   process these messages.
    //
    // Consequently, it is non-deterministic whether this test exercises the hanging-get case, or
    // the value-already-available case.
    //
    //  Note, however, that we _do_ expect the `get_update()` request to be _sent_ before the
    // `set_region()` request, as the FIDL bindings send the request before returning the Future.
    const REGION: &'static str = "AA";
    let watch = watcher.get_update();
    configurator.set_region(REGION)?;
    assert_eq!(REGION, watch.await?);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn from_none_state_sending_set_then_get_yields_expected_region() -> Result<(), Error> {
    // Set up handles.
    let test_context = new_test_context()?;
    let (configurator, watcher) = (&test_context.configurator, &test_context.watcher);

    // **Caution**
    //
    // * Because `get_update()` and `set_region()` are sent on separate channels, we don't know the
    //   order in which they'll arrive at the service.
    // * Additionally, we don't have any guarantees about the order in which the service will
    //   process these messages.
    //
    // Consequently, it is non-deterministic whether this test exercises the hanging-get case, or
    // the value-already-available case.
    const REGION: &'static str = "AA";
    configurator.set_region(REGION)?;
    assert_eq!(REGION, watcher.get_update().await?);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn from_some_state_sending_get_then_set_yields_expected_region() -> Result<(), Error> {
    // Set up handles.
    let test_context = new_test_context()?;
    let (configurator, watcher) = (&test_context.configurator, &test_context.watcher);

    // Move the service from the None state to the Some state.
    const FIRST_REGION: &'static str = "AA";
    configurator.set_region(FIRST_REGION)?;
    watcher.get_update().await?;

    // **Caution**
    //
    // * Because `get_update()` and `set_region()` are sent on separate channels, we don't know the
    //   order in which they'll arrive at the service.
    // * Additionally, we don't have any guarantees about the order in which the service will
    //   process these messages.
    //
    // Consequently, it is non-deterministic whether this test exercises the hanging-get case, or
    // the value-already-available case.
    //
    // Note, however, that we _do_ expect the `get_update()` request to be _sent_ before the
    // `set_region()` request, as the FIDL bindings send the request before returning the Future.
    const SECOND_REGION: &'static str = "BB";
    let watch = watcher.get_update();
    configurator.set_region(SECOND_REGION)?;
    assert_eq!(SECOND_REGION, watch.await?);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn from_some_state_sending_set_then_get_yields_expected_region() -> Result<(), Error> {
    // Set up handles.
    let test_context = new_test_context()?;
    let (configurator, watcher) = (&test_context.configurator, &test_context.watcher);

    // Move the service from the None state to the Some state.
    const FIRST_REGION: &'static str = "AA";
    configurator.set_region(FIRST_REGION)?;
    watcher.get_update().await?;

    // **Caution**
    //
    // * Because `get_update()` and `set_region()` are sent on separate channels, we don't know the
    //   order in which they'll arrive at the service.
    // * Additionally, we don't have any guarantees about the order in which the service will
    //   process these messages.
    //
    // Consequently, it is non-deterministic whether this test exercises the hanging-get case, or
    // the value-already-available case.
    const SECOND_REGION: &'static str = "BB";
    configurator.set_region(SECOND_REGION)?;
    assert_eq!(SECOND_REGION, watcher.get_update().await?);
    Ok(())
}

// Bundles together the handles needed to communicate with the Configurator and Watcher protocols.
// These items are bundled together to ensure that `launcher` and `region_service` outlive the
// protocols instances. Without that guarantee, the process backing the protocols my terminate
// prematurely.
struct TestContext {
    _launcher: LauncherProxy, // May be unread; exists primarily for lifetime management.
    _region_service: App,     // May be unread; exists primarily for lifetime management.
    configurator: ConfigProxy,
    watcher: WatcherProxy,
}

fn new_test_context() -> Result<TestContext, Error> {
    let launcher = launcher().context("Failed to open launcher service")?;
    let region_service = launch(&launcher, COMPONENT_URL.to_string(), None)
        .context("Failed to launch region service")?;
    let configurator = region_service
        .connect_to_service::<RegulatoryRegionConfiguratorMarker>()
        .context("Failed to connect to Configurator protocol")?;
    let watcher = region_service
        .connect_to_service::<RegulatoryRegionWatcherMarker>()
        .context("Failed to connect to Watcher protocol")?;
    Ok(TestContext { _launcher: launcher, _region_service: region_service, configurator, watcher })
}
