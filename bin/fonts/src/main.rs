// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(futures_api)]

#[macro_use]
extern crate failure;
extern crate fdio;
extern crate fidl;
extern crate fidl_fuchsia_fonts;
extern crate fidl_fuchsia_mem;
extern crate fuchsia_app as component;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate serde;
#[macro_use]
extern crate serde_derive;
extern crate serde_json;

mod font_service;
mod manifest;

use component::server::ServicesServer;
use failure::{Error, ResultExt};
use fidl::endpoints2::ServiceMarker;
use fidl_fuchsia_fonts::FontProviderMarker;
use font_service::FontService;
use std::sync::Arc;

fn main() -> Result<(), Error> {
    let mut executor =
        async::Executor::new().context("Creating async executor for Font service failed")?;
    let service = Arc::new(FontService::new()?);
    let fut = ServicesServer::new()
        .add_service((FontProviderMarker::NAME, move |channel| {
            font_service::spawn_server(service.clone(), channel)
        })).start()
        .context("Creating ServicesServer for Font service failed")?;
    executor
        .run_singlethreaded(fut)
        .context("Attempt to start up Font service on async::Executor failed")?;
    Ok(())
}
