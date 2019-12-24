// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::start_server;

use {
    anyhow::Error,
    fuchsia_syslog::{self as syslog, fx_log_info},
    std::fs,
};

mod net_settings_types;
mod server;

const LISTEN_IP: &str = "0.0.0.0";
const LISTEN_PORT: &str = "80";

const CLIENT_FRONTEND_PAGE: &str = "/pkg/data/index.html";

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["network_settings_server"])?;
    fx_log_info!("Starting network settings server.");

    let address = format!("{}:{}", LISTEN_IP, LISTEN_PORT);
    fx_log_info!("Listening on: {:?}", address);

    let contents: String = fs::read_to_string(CLIENT_FRONTEND_PAGE)?.parse()?;
    fx_log_info!("Read from file: {}", contents);

    start_server(address, contents);
}
