// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![feature(async_await, await_macro, futures_api)]

use {
    crate::mutation::*,
    crate::setting_adapter::SettingAdapter,
    failure::Error,
    fidl_fuchsia_setui::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as syslog, fx_log_info},
    futures::StreamExt,
    log::error,
    setui_handler::SetUIHandler,
    std::sync::Arc,
};

mod common;
mod fidl_clone;
mod mutation;
mod setting_adapter;
mod setui_handler;

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["setui-service"]).expect("Can't init logger");
    fx_log_info!("Starting setui-service...");

    let mut executor = fasync::Executor::new()?;

    let mut fs = ServiceFs::new();
    let handler = Arc::new(SetUIHandler::new());

    // TODO(SU-210): Remove once other adapters are ready.
    handler.register_adapter(Box::new(SettingAdapter::new(
        SettingType::Unknown,
        Box::new(process_string_mutation),
        None,
    )));

    fs.dir("public").add_fidl_service(move |stream: SetUiServiceRequestStream| {
        let handler_clone = handler.clone();
        fx_log_info!("Connecting to setui_service");
        fasync::spawn(
            async move {
                await!(handler_clone.handle_stream(stream))
                    .unwrap_or_else(|e| error!("Failed to spawn {:?}", e))
            },
        );
    });

    fs.take_and_serve_directory_handle()?;
    let () = executor.run_singlethreaded(fs.collect());
    Ok(())
}
