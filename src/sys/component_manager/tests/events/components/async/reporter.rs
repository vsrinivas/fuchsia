// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::{connect_to_service, create_scoped_dynamic_instance},
    test_utils_lib::events::{Destroyed, Event, EventSource, Started},
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Track all the starting child components.
    let event_source = EventSource::new_async()?;
    let mut event_stream = event_source.subscribe(vec![Started::TYPE, Destroyed::TYPE]).await?;

    let mut instances = vec![];
    let url =
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/stub_component.cm".to_string();
    for _ in 1..=3 {
        let scoped_instance =
            create_scoped_dynamic_instance("coll".to_string(), url.clone()).await?;
        instances.push(scoped_instance);
    }

    // Dropping instances destroys the children.
    drop(instances);

    let echo = connect_to_service::<fecho::EchoMarker>()?;

    for _ in 1..=3 {
        let _ = event_stream.expect_type::<Started>().await?;
        let _ = echo.echo_string(Some(&format!("{:?}", Started::TYPE))).await?;
    }

    for _ in 1..=3 {
        let _ = event_stream.expect_type::<Destroyed>().await?;
        let _ = echo.echo_string(Some(&format!("{:?}", Destroyed::TYPE))).await?;
    }

    Ok(())
}
