// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    breakpoint_system_client::*,
    failure::{Error, ResultExt},
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    hub_report::HubReport,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let hub_report = HubReport::new()?;

    // Read the listing of entries of the hub rooted at this component and
    // pass the results to the integration test.
    hub_report.report_directory_contents("/hub").await?;

    // Read the listing of the children of the parent from its hub, and pass the
    // results to the integration test.
    hub_report.report_directory_contents("/parent_hub/children").await?;

    // Read the content of the resolved_url file in the sibling hub, and pass the
    // results to the integration test.
    hub_report.report_file_content("/sibling_hub/exec/resolved_url").await?;

    // Read the listing of the used services and pass the results to the integration test.
    hub_report.report_directory_contents("/hub/used/svc").await?;

    let breakpoint_system = BreakpointSystemClient::new()?;
    let receiver = breakpoint_system.register(vec![UseCapability::TYPE]).await?;

    // Connect to the Echo capability.
    connect_to_service::<fecho::EchoMarker>().context("error connecting to Echo service")?;

    // Since connecting to the Echo capability is an asynchronous operation, we should
    // wait until the capability is actually in use.
    let invocation = receiver
        .wait_until_use_capability("/reporter:0", "/svc/fidl.examples.routing.echo.Echo")
        .await?;
    invocation.resume().await?;

    // Read the listing of the used capabilities and pass the results to the integration test.
    hub_report.report_directory_contents("/hub/used/svc").await?;

    // Connect to the Echo capability again
    connect_to_service::<fecho::EchoMarker>().context("error connecting to Echo service")?;

    // Since connecting to the Echo capability is an asynchronous operation, we should
    // wait until the capability is actually in use.
    let invocation = receiver
        .wait_until_use_capability("/reporter:0", "/svc/fidl.examples.routing.echo.Echo")
        .await?;
    invocation.resume().await?;

    // Read the listing of the used capabilities and pass the results to the integration test.
    hub_report.report_directory_contents("/hub/used/svc").await?;

    Ok(())
}
