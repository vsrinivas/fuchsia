// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod ime;
mod ime_service;

use failure::{Error, ResultExt};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_ui_input as uii;
use fidl_fuchsia_ui_input::{ImeServiceMarker, ImeVisibilityServiceMarker};
use fuchsia_app::server::ServicesServer;
use fuchsia_syslog;
use futures::prelude::*;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["ime_service"]).expect("ime syslog init should not fail");
    let mut executor = fuchsia_async::Executor::new()
        .context("Creating fuchsia_async executor for IME service failed")?;
    let ime_service = ime_service::ImeService::new();
    let ime_service1 = ime_service.clone();
    let ime_service2 = ime_service.clone();
    let done = ServicesServer::new()
        .add_service((ImeServiceMarker::NAME, move |chan| {
            fuchsia_async::spawn(
                uii::ImeService::serve(ime_service1.clone(), chan).map(|res| res.unwrap()),
            );
        })).add_service((ImeVisibilityServiceMarker::NAME, move |chan| {
            fuchsia_async::spawn(
                uii::ImeVisibilityService::serve(ime_service2.clone(), chan)
                    .map(|res| res.unwrap()),
            );
        })).start()
        .context("Creating ServicesServer for IME service failed")?;
    executor
        .run_singlethreaded(done)
        .context("Attempt to start up IME services on async::Executor failed")?;

    Ok(())
}
