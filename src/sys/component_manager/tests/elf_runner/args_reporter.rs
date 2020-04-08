// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fidl_examples_routing_echo as fecho, fuchsia_component::client::connect_to_service};

#[fuchsia_async::run_singlethreaded()]
async fn main() {
    fuchsia_syslog::init_with_tags(&["args_reporter"]).expect("failed to init syslog");

    let echo = connect_to_service::<fecho::EchoMarker>().expect("failed to connect to args report");
    let args: Vec<String> = std::env::args().collect();
    // We're using this to pass a string back to the test, we don't really care what it returns
    // here
    let _ = echo.echo_string(Some(&format!("{}", args.join(" ")))).await;
}
