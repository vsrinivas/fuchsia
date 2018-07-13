// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
extern crate failure;
extern crate fidl;
extern crate fidl_fuchsia_ui_input;
extern crate fuchsia_app as component;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;

mod ime;
mod ime_service;

use component::server::ServicesServer;
use failure::{Error, ResultExt};
use fidl::endpoints2::ServiceMarker;
use fidl_fuchsia_ui_input::ImeServiceMarker;

fn main() -> Result<(), Error> {
    let mut executor =
        async::Executor::new().context("Creating async executor for IME service failed")?;
    let fut = ServicesServer::new()
        .add_service((ImeServiceMarker::NAME, move |chan| {
            ime_service::spawn_server(chan)
        }))
        .start()
        .context("Creating ServicesServer for IME service failed")?;
    executor
        .run_singlethreaded(fut)
        .context("Attempt to start up IME service on async::Executor failed")?;
    Ok(())
}
