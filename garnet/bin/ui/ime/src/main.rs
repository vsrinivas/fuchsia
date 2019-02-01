// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]

mod ime;
mod ime_service;
#[cfg(test)]
mod test_helpers;

use failure::{Error, ResultExt};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_ui_input::{ImeServiceMarker, ImeVisibilityServiceMarker};
use fuchsia_app::server::ServicesServer;
use fuchsia_syslog;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["ime_service"]).expect("ime syslog init should not fail");
    let mut executor = fuchsia_async::Executor::new()
        .context("Creating fuchsia_async executor for IME service failed")?;
    let ime_service = ime_service::ImeService::new();
    let ime_service1 = ime_service.clone();
    let ime_service2 = ime_service.clone();
    let done = ServicesServer::new()
        .add_service((ImeServiceMarker::NAME, move |chan| {
            ime_service1.bind_ime_service(chan);
        }))
        .add_service((ImeVisibilityServiceMarker::NAME, move |chan| {
            ime_service2.bind_ime_visibility_service(chan);
        }))
        .start()
        .context("Creating ServicesServer for IME service failed")?;
    executor
        .run_singlethreaded(done)
        .context("Attempt to start up IME services on async::Executor failed")?;

    Ok(())
}
