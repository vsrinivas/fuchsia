// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    test_utils_lib::events::{BeforeStartInstance, Event, EventSource, Handler},
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Track all the starting child components.
    let event_source = EventSource::new()?;
    let event_stream = event_source.subscribe(vec![BeforeStartInstance::TYPE]).await?;

    event_source.start_component_tree().await?;

    let echo = connect_to_service::<fecho::EchoMarker>()?;

    for _ in 1..=3 {
        let event = event_stream.expect_type::<BeforeStartInstance>().await?;
        let target_moniker = event.target_moniker();
        let _ = echo.echo_string(Some(target_moniker)).await?;
        event.resume().await?;
    }

    Ok(())
}
