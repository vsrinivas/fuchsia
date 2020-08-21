// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START imports]
use anyhow::{Context as _, Error};
use fidl_fuchsia_examples::{EchoEvent, EchoMarker};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use futures::prelude::*;
// [END imports]

// [START main]
#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Connect to the Echo protocol, which is assumed to be in the component's environment
    let echo = connect_to_service::<EchoMarker>().context("Failed to connect to echo service")?;

    // Make an EchoString request and wait for the response
    let res = echo.echo_string("hello").await?;
    println!("response: {:?}", res);

    // Make a SendString request
    echo.send_string("hi")?;
    // Wait for a single OnString event
    let EchoEvent::OnString { response } =
        echo.take_event_stream().next().await.context("error receiving events")??;
    println!("Received OnString event for string {:?}", response);

    Ok(())
}
// [START main]
