// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol,
    fidl_fuchsia_test_manager as ftest_manager,
};

const RUN_BUILDER_SELECTOR: &str = "core/test_manager:expose:fuchsia.test.manager.RunBuilder";
const QUERY_SELECTOR: &str = "core/test_manager:expose:fuchsia.test.manager.Query";
/// Timeout for connecting to test manager. This is a longer timeout than the timeout given for
/// connecting to other protocols, as during the first run
const TIMEOUT: std::time::Duration = std::time::Duration::from_secs(45);

/// Connect to `fuchsia.test.manager.RunBuilder` on a target device using an RCS connection.
pub async fn connect_to_run_builder(
    remote_control: &fremotecontrol::RemoteControlProxy,
) -> Result<ftest_manager::RunBuilderProxy> {
    connect_to_protocol::<ftest_manager::RunBuilderMarker>(RUN_BUILDER_SELECTOR, remote_control)
        .await
}

/// Connect to `fuchsia.test.manager.Query` on a target device using an RCS connection.
pub async fn connect_to_query(
    remote_control: &fremotecontrol::RemoteControlProxy,
) -> Result<ftest_manager::QueryProxy> {
    connect_to_protocol::<ftest_manager::QueryMarker>(QUERY_SELECTOR, remote_control).await
}

async fn connect_to_protocol<P: ProtocolMarker>(
    selector: &'static str,
    remote_control: &fremotecontrol::RemoteControlProxy,
) -> Result<P::Proxy> {
    let (proxy, server_end) =
        fidl::endpoints::create_proxy::<P>().context("failed to create proxy")?;
    rcs::connect_with_timeout(TIMEOUT, selector, remote_control, server_end.into_channel()).await?;
    Ok(proxy)
}
