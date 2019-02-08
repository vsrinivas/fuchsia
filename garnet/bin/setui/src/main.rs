// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![feature(async_await, await_macro, futures_api)]

use {
    failure::Error,
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_setui::SetUiServiceMarker,
    fuchsia_app::server::ServicesServer,
    fuchsia_async as fasync,
    fuchsia_syslog::{self as syslog, fx_log_info},
    futures::TryFutureExt,
};

mod setui_service;

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["setui-service"]).expect("Can't init logger");
    fx_log_info!("Starting setui-service...");

    let mut executor = fasync::Executor::new()?;

    let server = ServicesServer::new()
        .add_service((SetUiServiceMarker::NAME, move |channel: fasync::Channel| {
            spawn_setui_service(channel)
        }))
        .start()?;

    executor.run_singlethreaded(server)
}

fn spawn_setui_service(channel: fasync::Channel) {
    fx_log_info!("Connecting to setui_service");
    fasync::spawn(
        setui_service::start_setui_service(channel)
            .unwrap_or_else(|e| eprintln!("Failed to spawn {:?}", e)),
    )
}
