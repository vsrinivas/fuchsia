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
    fidl_fuchsia_sys_test as systest, fuchsia_async as fasync,
    fuchsia_component::{
        client::{connect_to_service, launch, launcher, App},
        fuchsia_single_component_package_url,
    },
};

const COMPONENT_URL: &str = fuchsia_single_component_package_url!("regulatory_region");

#[fasync::run_singlethreaded(test)]
async fn from_none_state_sending_get_region_then_set_yields_expected_region() -> Result<(), Error> {
    // Set up handles.
    let test_context = new_test_context().await?;
    let (configurator, watcher) = (&test_context.configurator, &test_context.watcher);

    // Get the initial value so that it doesn't matter whether set or get is handled first in the
    // rest of the test.
    assert_eq!(None, watcher.get_region_update().await?);

    // **Caution**
    //
    // * Because `get_region_update()` and `set_region()` are sent on separate channels, we don't know the
    //   order in which they'll arrive at the service.
    // * Additionally, we don't have any guarantees about the order in which the service will
    //   process these messages.
    //
    // Consequently, it is non-deterministic whether this test exercises the hanging-get case, or
    // the update-already-available case.
    //
    //  Note, however, that we _do_ expect the `get_region_update()` request to be _sent_ before the
    // `set_region()` request, as the FIDL bindings send the request before returning the Future.
    const REGION: &'static str = "AA";
    let watch = watcher.get_region_update();
    configurator.set_region(REGION)?;
    assert_eq!(Some(REGION.to_string()), watch.await?);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn from_none_state_sending_set_then_get_region_yields_expected_region() -> Result<(), Error> {
    // Set up handles.
    let test_context = new_test_context().await?;
    let (configurator, watcher) = (&test_context.configurator, &test_context.watcher);

    // Get the initial value so that it doesn't matter whether set or get is handled first in the
    // rest of the test.
    assert_eq!(None, watcher.get_region_update().await?);

    // **Caution**
    //
    // * Because `get_region_update()` and `set_region()` are sent on separate channels, we don't know the
    //   order in which they'll arrive at the service.
    // * Additionally, we don't have any guarantees about the order in which the service will
    //   process these messages.
    //
    // Consequently, it is non-deterministic whether this test exercises the hanging-get case, or
    // the update-already-available case.
    const REGION: &'static str = "AA";
    configurator.set_region(REGION)?;
    assert_eq!(Some(REGION.to_string()), watcher.get_region_update().await?);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn from_some_state_sending_get_region_then_set_yields_expected_region() -> Result<(), Error> {
    // Set up handles.
    let test_context = new_test_context().await?;
    let (configurator, watcher) = (&test_context.configurator, &test_context.watcher);

    // Get the initial value so that it doesn't matter whether set or get is handled first in the
    // rest of the test.
    assert_eq!(None, watcher.get_region_update().await?);

    // Move the service from the None state to the Some state.
    const FIRST_REGION: &'static str = "AA";
    configurator.set_region(FIRST_REGION)?;
    watcher.get_region_update().await?;

    // **Caution**
    //
    // * Because `get_region_update()` and `set_region()` are sent on separate channels, we don't know the
    //   order in which they'll arrive at the service.
    // * Additionally, we don't have any guarantees about the order in which the service will
    //   process these messages.
    //
    // Consequently, it is non-deterministic whether this test exercises the hanging-get case, or
    // the update-already-available case.
    //
    // Note, however, that we _do_ expect the `get_region_update()` request to be _sent_ before the
    // `set_region()` request, as the FIDL bindings send the request before returning the Future.
    const SECOND_REGION: &'static str = "BB";
    let watch = watcher.get_region_update();
    configurator.set_region(SECOND_REGION)?;
    assert_eq!(Some(SECOND_REGION.to_string()), watch.await?);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn from_some_state_sending_set_then_get_region_yields_expected_region() -> Result<(), Error> {
    // Set up handles.
    let test_context = new_test_context().await?;
    let (configurator, watcher) = (&test_context.configurator, &test_context.watcher);

    // Get the initial value so that it doesn't matter whether set or get is handled first in the
    // rest of the test.
    assert_eq!(None, watcher.get_region_update().await?);

    // Move the service from the None state to the Some state.
    const FIRST_REGION: &'static str = "AA";
    configurator.set_region(FIRST_REGION)?;
    watcher.get_region_update().await?;

    // **Caution**
    //
    // * Because `get_region_update()` and `set_region()` are sent on separate channels, we don't know the
    //   order in which they'll arrive at the service.
    // * Additionally, we don't have any guarantees about the order in which the service will
    //   process these messages.
    //
    // Consequently, it is non-deterministic whether this test exercises the hanging-get case, or
    // the update-already-available case.
    const SECOND_REGION: &'static str = "BB";
    configurator.set_region(SECOND_REGION)?;
    assert_eq!(Some(SECOND_REGION.to_string()), watcher.get_region_update().await?);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn from_none_state_sending_get_region_yields_none() -> Result<(), Error> {
    // Set up handles.
    let test_context = new_test_context().await?;
    let (_configurator, watcher) = (&test_context.configurator, &test_context.watcher);

    // The initial update before setting anything should be None.
    assert_eq!(None, watcher.get_region_update().await?);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn from_some_state_reloading_service_yields_expected_region() -> Result<(), Error> {
    // Set up handles.
    let test_context = new_test_context().await?;
    let (configurator, watcher) = (&test_context.configurator, &test_context.watcher);

    // Get the initial value so that it doesn't matter whether set or get is handled first in the
    // rest of the test. Ignore the value because it depends on what ran previously.
    watcher.get_region_update().await?;

    const SECOND_REGION: &'static str = "CC";
    configurator.set_region(SECOND_REGION)?;
    assert_eq!(Some(SECOND_REGION.to_string()), watcher.get_region_update().await?);

    // Restart the service backing the protocols so that it will read the cached value.
    let test_context = new_test_context_without_clear()?;
    let watcher = &test_context.watcher;
    assert_eq!(Some(SECOND_REGION.to_string()), watcher.get_region_update().await?);
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

async fn new_test_context() -> Result<TestContext, Error> {
    // NOTE: this clears isolated-cache-storage in order to clear the regulatory region cache, but
    // it will also clear everything in cache.
    let cache_control = connect_to_service::<systest::CacheControlMarker>()?;
    cache_control.clear().await.context("Failed to clear cache")?;
    new_test_context_without_clear()
}

/// Set up the protocol services for the without clearing the regulatory region cache.
fn new_test_context_without_clear() -> Result<TestContext, Error> {
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

// The tests below are for the deprecated get_update function

#[fasync::run_singlethreaded(test)]
async fn from_none_state_sending_get_then_set_yields_expected_region() -> Result<(), Error> {
    // Set up handles.
    let test_context = new_test_context().await?;
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
    assert_eq!(REGION.to_string(), watch.await?);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn from_none_state_sending_set_then_get_yields_expected_region() -> Result<(), Error> {
    // Set up handles.
    let test_context = new_test_context().await?;
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
    assert_eq!(REGION.to_string(), watcher.get_update().await?);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn from_some_state_sending_get_then_set_yields_expected_region() -> Result<(), Error> {
    // Set up handles.
    let test_context = new_test_context().await?;
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
    assert_eq!(SECOND_REGION.to_string(), watch.await?);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn from_some_state_sending_set_then_get_yields_expected_region() -> Result<(), Error> {
    // Set up handles.
    let test_context = new_test_context().await?;
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
    assert_eq!(SECOND_REGION.to_string(), watcher.get_update().await?);
    Ok(())
}
