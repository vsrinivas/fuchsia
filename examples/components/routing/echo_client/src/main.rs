// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol, std::env, tracing::info,
};

#[fasync::run_singlethreaded]
#[fuchsia::component]
async fn main() {
    let mut args: Vec<String> = env::args().collect();
    args.remove(0);
    let echo_string = args.join(" ");

    let echo = connect_to_protocol::<fecho::EchoMarker>().expect("error connecting to echo");
    let out = echo.echo_string(Some(&echo_string)).await.expect("echo_string failed");
    info!("{}", out.as_ref().expect("echo_string got empty result"));
}
