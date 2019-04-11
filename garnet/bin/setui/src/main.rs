// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![feature(async_await, await_macro, futures_api)]

use {
    failure::Error,
    fidl_fuchsia_setui::SetUiServiceRequestStream,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as syslog, fx_log_info},
    futures::{StreamExt, TryFutureExt},
};

mod setui_service;

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["setui-service"]).expect("Can't init logger");
    fx_log_info!("Starting setui-service...");

    let mut executor = fasync::Executor::new()?;

    let mut fs = ServiceFs::new();
    fs.dir("public").add_fidl_service(spawn_setui_service);
    fs.take_and_serve_directory_handle()?;

    let () = executor.run_singlethreaded(fs.collect());
    Ok(())
}

fn spawn_setui_service(stream: SetUiServiceRequestStream) {
    fx_log_info!("Connecting to setui_service");
    fasync::spawn(
        setui_service::start_setui_service(stream)
            .unwrap_or_else(|e| eprintln!("Failed to spawn {:?}", e)),
    )
}
