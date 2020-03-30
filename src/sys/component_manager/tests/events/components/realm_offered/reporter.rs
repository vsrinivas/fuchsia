// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fidl_examples_routing_echo as fecho, fidl_fidl_test_components as ftest,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    test_utils_lib::events::{Event, EventSource, Handler, Started},
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Track all the starting components.
    let event_source = EventSource::new_sync()?;
    let mut event_stream = event_source.subscribe(vec![Started::TYPE]).await?;

    event_source.start_component_tree().await?;

    let echo = connect_to_service::<fecho::EchoMarker>()?;

    // Connect to the parent offered Trigger. The parent will start the lazy child components and
    // this component should know about their started events given that it was offered those
    // events.
    let trigger =
        connect_to_service::<ftest::TriggerMarker>().expect("error connecting to trigger");
    trigger.run().await.expect("start trigger failed");

    for _ in 0..3 {
        let event = event_stream.expect_type::<Started>().await?;
        let target_moniker = event.target_moniker();
        let _ = echo.echo_string(Some(target_moniker)).await?;
        event.resume().await?;
    }

    Ok(())
}
