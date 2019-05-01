// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![deny(warnings)]

mod app;
mod view_controller;

use app::App;
use failure::{Error, ResultExt};
use fuchsia_async as fasync;
use futures::StreamExt;
use std::env;

fn main() -> Result<(), Error> {
    env::set_var("RUST_BACKTRACE", "full");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let app = App::new()?;

    let mut fs = fuchsia_component::server::ServiceFs::new_local();
    fs.dir("public").add_fidl_service(|stream| App::spawn_view_provider_server(&app, stream));
    fs.take_and_serve_directory_handle()?;

    let () = executor.run_singlethreaded(fs.collect());
    Ok(())
}
