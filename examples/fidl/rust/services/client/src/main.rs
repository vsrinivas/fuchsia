// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_examples::EchoServiceMarker;
use fuchsia_component::client::connect_to_service;

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    let echo =
        connect_to_service::<EchoServiceMarker>().context("Failed to connect to echo service")?;

    let regular = echo.regular_echo().context("failed to connect to regular_echo member")?;
    let regular_response = regular.echo_string("hello world!").await?;
    println!("regular response: {:?}", regular_response);

    let reversed = echo.reversed_echo().context("failed to connect to reversed_echo member")?;
    let reversed_response = reversed.echo_string("hello world!").await?;
    println!("reversed response: {:?}", reversed_response);

    Ok(())
}
