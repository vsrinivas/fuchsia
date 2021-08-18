// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fidl_examples_echo as fecho, fuchsia_component::client::connect_to_protocol};

#[fuchsia::test]
async fn test_echo() {
    const ECHO_STRING: &str = "Hello, world!";
    let echo = connect_to_protocol::<fecho::EchoMarker>().expect("error connecting to echo");
    let out = echo.echo_string(Some(ECHO_STRING)).await.expect("echo_string failed");
    assert_eq!(ECHO_STRING, out.unwrap());
}
