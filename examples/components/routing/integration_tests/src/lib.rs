// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{self, Error},
    fidl_fidl_examples_routing_echo as fecho,
    fuchsia_component::client,
};

#[fuchsia::test]
async fn echo_integration_test() -> Result<(), Error> {
    const ECHO_STRING: &str = "Hello, world!";

    let echo = client::connect_to_protocol::<fecho::EchoMarker>()
        .expect("error connecting to echo server");
    let out = echo.echo_string(Some(ECHO_STRING)).await.expect("echo_string failed");

    assert_eq!(ECHO_STRING, out.unwrap());
    Ok(())
}
