// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(futures_api)]

mod font_service;
mod manifest;

use self::font_service::FontService;
use failure::{Error, ResultExt};
use fidl::endpoints2::ServiceMarker;
use fidl_fuchsia_fonts::ProviderMarker as FontProviderMarker;
use fuchsia_app::server::ServicesServer;
use std::sync::Arc;

fn main() -> Result<(), Error> {
    let mut executor = fuchsia_async::Executor::new()
        .context("Creating async executor for Font service failed")?;
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
