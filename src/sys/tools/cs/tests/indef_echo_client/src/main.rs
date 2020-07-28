// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err, fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service, fuchsia_syslog as syslog, log::*,
};

#[fasync::run_singlethreaded]
async fn main() {
    syslog::init_with_tags(&["indef_echo_client"]).expect("failed to initialize logger");
    let echo = connect_to_service::<fecho::EchoMarker>().expect("error connecting to echo");

    // This is an infinite loop because it gives cs2 enough time to query
    // the state of the world from the hub.
    loop {
        let out = echo.echo_string(Some("Hippos rule!")).await.expect("echo_string failed");
        info!("{}", out.ok_or(format_err!("empty result")).expect("echo_string got empty result"));
        std::thread::sleep(std::time::Duration::from_secs(5));
    }
}
