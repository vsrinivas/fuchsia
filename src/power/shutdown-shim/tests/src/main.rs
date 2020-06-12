// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fidl_fuchsia_test_shutdownshim as test_shutdown_shim, fuchsia_async as fasync,
    fuchsia_component::client as fclient,
};

#[fasync::run_singlethreaded(test)]
async fn power_manager_present() -> Result<(), Error> {
    let tests_proxy = fclient::connect_to_service::<test_shutdown_shim::TestsMarker>()?;
    tests_proxy.power_manager_present().await?;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_missing() -> Result<(), Error> {
    let tests_proxy = fclient::connect_to_service::<test_shutdown_shim::TestsMarker>()?;
    tests_proxy.power_manager_missing().await?;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn power_manager_not_present() -> Result<(), Error> {
    let tests_proxy = fclient::connect_to_service::<test_shutdown_shim::TestsMarker>()?;
    tests_proxy.power_manager_not_present().await?;
    Ok(())
}
