// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol, tracing::info,
};

#[fasync::run_singlethreaded]
#[fuchsia::component]
async fn main() {
    let echo = connect_to_protocol::<fecho::EchoMarker>().expect("error connecting to echo");
    let out = echo.echo_string(Some("Hippos rule!")).await.expect("echo_string failed");
    info!("{}", out.as_ref().expect("echo_string got empty result"));
}
