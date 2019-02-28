// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]

mod app;
mod view_controller;

use app::App;
use failure::{Error, ResultExt};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_ui_app as viewsv2;
use fidl_fuchsia_ui_viewsv1::ViewProviderMarker;
use fuchsia_async as fasync;
use std::env;

fn main() -> Result<(), Error> {
    env::set_var("RUST_BACKTRACE", "full");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let app_for_v1 = App::new()?;
    let app_for_v2 = app_for_v1.clone();

    let fut = fuchsia_app::server::ServicesServer::new()
        .add_service((ViewProviderMarker::NAME, move |chan| {
            App::spawn_v1_view_provider_server(&app_for_v1, chan)
        }))
        .add_service((viewsv2::ViewProviderMarker::NAME, move |chan| {
            App::spawn_v2_view_provider_server(&app_for_v2, chan)
        }))
        .start()
        .context("Error starting view provider server")?;

    executor.run_singlethreaded(fut)?;
    Ok(())
}
