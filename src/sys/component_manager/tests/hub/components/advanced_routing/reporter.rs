// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_side_testing::*,
    failure::{Error, ResultExt},
    fidl_fidl_examples_routing_echo as fecho, fidl_fuchsia_test_breakpoints as fbreak,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let testing = ComponentSideTesting::new()?;

    testing.register_breakpoints(vec![fbreak::EventType::UseCapability]).await?;

    // Read the listing of entires of the hub rooted at this component and
    // pass the results to the integration test.
    testing.report_directory_contents("/hub").await?;

    // Read the listing of the children of the parent from its hub, and pass the
    // results to the integration test.
    testing.report_directory_contents("/parent_hub/children").await?;

    // Read the content of the resolved_url file in the sibling hub, and pass the
    // results to the integration test.
    testing.report_file_content("/sibling_hub/exec/resolved_url").await?;

    // Read the listing of the used services and pass the results to the integration test.
    testing.report_directory_contents("/hub/used/svc").await?;

    // Connect to the Echo capability.
    connect_to_service::<fecho::EchoMarker>().context("error connecting to Echo service")?;

    // Since connecting to the Echo capability is an asynchronous operation, we should
    // wait until the capability is actually in use.
    testing
        .wait_until_use_capability(vec!["reporter:0"], "/svc/fidl.examples.routing.echo.Echo")
        .await?;
    testing.resume_invocation().await?;

    // Read the listing of the used capabilities and pass the results to the integration test.
    testing.report_directory_contents("/hub/used/svc").await?;

    // Connect to the Echo capability again
    connect_to_service::<fecho::EchoMarker>().context("error connecting to Echo service")?;

    // Since connecting to the Echo capability is an asynchronous operation, we should
    // wait until the capability is actually in use.
    testing
        .wait_until_use_capability(vec!["reporter:0"], "/svc/fidl.examples.routing.echo.Echo")
        .await?;
    testing.resume_invocation().await?;

    // Read the listing of the used capabilities and pass the results to the integration test.
    testing.report_directory_contents("/hub/used/svc").await?;

    Ok(())
}
