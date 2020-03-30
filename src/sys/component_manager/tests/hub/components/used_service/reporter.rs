// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    hub_report::HubReport,
    test_utils_lib::events::*,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Accessing the hub triggers a CapabilityRouted event.
    // Hence, code is placed in blocks so that event_streams are dropped.

    let event_source = EventSource::new_sync()?;
    event_source.start_component_tree().await?;

    let hub_report = {
        // Subscribes to CapabilityRouted events
        let mut event_stream = event_source.subscribe(vec![CapabilityRouted::TYPE]).await?;

        // Connect to the HubReport service
        let hub_report = HubReport::new()?;

        // Wait until the HubReport service has been routed successfully
        event_stream
            .wait_until_framework_capability(".", "/svc/fuchsia.test.hub.HubReport", Some("."))
            .await?
            .resume()
            .await?;

        hub_report
    };

    // Read the listing of the used services and pass the results to the integration test.
    hub_report.report_directory_contents("/hub/exec/used/svc").await?;

    {
        // Subscribes to CapabilityRouted events
        let mut event_stream = event_source.subscribe(vec![CapabilityRouted::TYPE]).await?;

        // Connect to the Echo capability.
        connect_to_service::<fecho::EchoMarker>().context("error connecting to Echo service")?;

        // Since connecting to the Echo capability is an asynchronous operation, we should
        // wait until the capability is actually routed.
        event_stream
            .wait_until_component_capability(".", "/svc/fidl.examples.routing.echo.Echo")
            .await?
            .resume()
            .await?;
    }

    // Read the listing of the used capabilities and pass the results to the integration test.
    hub_report.report_directory_contents("/hub/exec/used/svc").await?;

    {
        // Subscribes to CapabilityRouted events
        let mut event_stream = event_source.subscribe(vec![CapabilityRouted::TYPE]).await?;

        // Connect to the Echo capability again
        connect_to_service::<fecho::EchoMarker>().context("error connecting to Echo service")?;

        // Since connecting to the Echo capability is an asynchronous operation, we should
        // wait until the capability is actually routed.
        event_stream
            .wait_until_component_capability(".", "/svc/fidl.examples.routing.echo.Echo")
            .await?
            .resume()
            .await?;
    }

    // Read the listing of the used capabilities and pass the results to the integration test.
    hub_report.report_directory_contents("/hub/exec/used/svc").await?;

    Ok(())
}
