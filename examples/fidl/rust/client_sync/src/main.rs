// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START imports]
use anyhow::{Context as _, Error};
use fidl_fuchsia_examples::{EchoEvent, EchoMarker, EchoSynchronousProxy};
use fuchsia_component::client::connect_channel_to_protocol;
use fuchsia_zircon as zx;
// [END imports]

// [START main]
fn main() -> Result<(), Error> {
    // Initialize a channel, and label the two ends as the server_end and client_end
    let (server_end, client_end) = zx::Channel::create()?;
    // Connect an implementation of the Echo protocol to the server end
    connect_channel_to_protocol::<EchoMarker>(server_end)
        .context("Failed to connect to echo service")?;
    // Create a synchronous proxy using the client end
    let echo = EchoSynchronousProxy::new(client_end);

    // Make an EchoString request, with no timeout for receiving the response
    let res = echo.echo_string("hello", zx::Time::INFINITE)?;
    println!("response: {:?}", res);

    // Make a SendString request
    echo.send_string("hi")?;
    // Wait for a single OnString event.
    let EchoEvent::OnString { response } =
        echo.wait_for_event(zx::Time::INFINITE).context("error receiving events")?;
    println!("Received OnString event for string {:?}", response);

    Ok(())
}
// [END main]
