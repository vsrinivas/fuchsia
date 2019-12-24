// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_syslog::{self as syslog, fx_log_info},
    rouille,
};

const LISTEN_IP: &str = "0.0.0.0";
const LISTEN_PORT: &str = "80";

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["example_http_server"])?;
    fx_log_info!("Starting example http server.");

    let address = format!("{}:{}", LISTEN_IP, LISTEN_PORT);
    fx_log_info!("Listening on: {:?}", address);

    rouille::start_server(address, move |request| rouille::match_assets(&request, "/pkg/data"))
}
