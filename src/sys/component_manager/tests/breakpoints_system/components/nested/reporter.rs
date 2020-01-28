// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    breakpoint_system_client::{BeforeStartInstance, BreakpointSystemClient, Handler, Invocation},
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Track all the starting child components.
    let breakpoint_system = BreakpointSystemClient::new()?;
    let receiver = breakpoint_system.set_breakpoints(vec![BeforeStartInstance::TYPE]).await?;

    breakpoint_system.start_component_tree().await?;

    let echo = connect_to_service::<fecho::EchoMarker>().expect("error connecting to echo");

    for _ in 1..=3 {
        let event = receiver.expect_type::<BeforeStartInstance>().await?;
        let target_moniker = event.target_moniker();
        let _ = echo.echo_string(Some(target_moniker)).await.expect("echo_string failed");
        event.resume().await?;
    }

    Ok(())
}
