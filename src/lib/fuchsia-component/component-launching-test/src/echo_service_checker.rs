// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fidl_examples_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

const TEST_STRING: &str = "test string";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let proxy =
        connect_to_service::<fecho::EchoMarker>().context("Failed to connect to echo service")?;
    let res = proxy.echo_string(Some(TEST_STRING)).await?;
    assert_eq!(res.as_deref(), Some(TEST_STRING));
    Ok(())
}
