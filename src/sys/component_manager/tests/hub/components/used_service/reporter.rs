// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    breakpoint_system_client::*,
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    hub_report::HubReport,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Accessing the hub triggers a RouteCapability event.
    // Hence, code is placed in blocks so that breakpoint receivers are dropped.

    // Connect to the Breakpoint
    let breakpoint_system = BreakpointSystemClient::new()?;

    let hub_report = {
        // Register for RouteCapability events
        let receiver = breakpoint_system.set_breakpoints(vec![RouteCapability::TYPE]).await?;

        // Connect to the HubReport service
        let hub_report = HubReport::new()?;

        // Wait until the HubReport service has been routed successfully
        receiver
            .wait_until_framework_capability(".", "/svc/fuchsia.test.hub.HubReport", Some("."))
            .await?
            .resume()
            .await?;

        hub_report
    };

    // Read the listing of the used services and pass the results to the integration test.
    hub_report.report_directory_contents("/hub/exec/used/svc").await?;

    {
        // Register for RouteCapability events
        let receiver = breakpoint_system.set_breakpoints(vec![RouteCapability::TYPE]).await?;

        // Connect to the Echo capability.
        connect_to_service::<fecho::EchoMarker>().context("error connecting to Echo service")?;

        // Since connecting to the Echo capability is an asynchronous operation, we should
        // wait until the capability is actually routed.
        receiver
            .wait_until_component_capability(".", "/svc/fidl.examples.routing.echo.Echo")
            .await?
            .resume()
            .await?;
    }

    // Read the listing of the used capabilities and pass the results to the integration test.
    hub_report.report_directory_contents("/hub/exec/used/svc").await?;

    {
        // Register for RouteCapability events
        let receiver = breakpoint_system.set_breakpoints(vec![RouteCapability::TYPE]).await?;

        // Connect to the Echo capability again
        connect_to_service::<fecho::EchoMarker>().context("error connecting to Echo service")?;

        // Since connecting to the Echo capability is an asynchronous operation, we should
        // wait until the capability is actually routed.
        receiver
            .wait_until_component_capability(".", "/svc/fidl.examples.routing.echo.Echo")
            .await?
            .resume()
            .await?;
    }

    // Read the listing of the used capabilities and pass the results to the integration test.
    hub_report.report_directory_contents("/hub/exec/used/svc").await?;

    Ok(())
}
