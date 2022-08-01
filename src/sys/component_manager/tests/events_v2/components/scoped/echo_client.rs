// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err, fidl_fidl_examples_routing_echo as fecho,
    fuchsia_component::client::connect_to_protocol,
};

#[fuchsia::main(logging_tags = ["scoped_echo_client"])]
async fn main() {
    let echo = connect_to_protocol::<fecho::EchoMarker>().expect("error connecting to echo");

    let out = echo.echo_string(Some("Hippos rule!")).await.expect("echo_string failed");
    let out = out.ok_or(format_err!("empty result")).expect("echo_string got empty result");

    assert_eq!(out, "Hippos rule!");
}
