// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl_test_placeholders::EchoMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::{launch, launcher};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let launcher = launcher().context("Failed to open launcher service")?;
    let app = launch(
        &launcher,
        "fuchsia-pkg://fuchsia.com/echo-integration-test#meta/echo-server.cmx".to_string(),
        None,
    )
    .context("Failed to launch echo service")?;

    let echo =
        app.connect_to_service::<EchoMarker>().context("Failed to connect to echo service")?;

    let res = echo.echo_string(Some("Hello world!")).await?;
    match res.as_ref().map(String::as_str) {
        Some("Hello world!") => Ok(()),
        Some(unexpected) => Err(format_err!("Got {}", unexpected)),
        None => Err(format_err!("Got none")),
    }
}
