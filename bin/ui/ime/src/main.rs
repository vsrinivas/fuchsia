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
use fidl::endpoints2::RequestStream;
use fidl::endpoints2::ServiceMarker;
use fidl_fuchsia_ui_input as uii;
use futures::prelude::*;
use uii::{ImeServiceMarker, ImeVisibilityServiceMarker};

fn main() -> Result<(), Error> {
    let mut executor =
        async::Executor::new().context("Creating async executor for IME service failed")?;
    let ime_service = ime_service::ImeService::new();
    let ime_service1 = ime_service.clone();
    let ime_service2 = ime_service.clone();
    let done = ServicesServer::new()
        .add_service((ImeServiceMarker::NAME, move |chan| {
            async::spawn(
                uii::ImeService::serve(ime_service1.clone(), chan).map(|res| res.unwrap()),
            );
        })).add_service((ImeVisibilityServiceMarker::NAME, move |chan| {
            async::spawn(
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
