// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![recursion_limit="256"]

#[macro_use]
mod log_error;
mod controller;
mod publisher;
mod service;
mod session;
#[cfg(test)]
mod test;

use self::controller::ControllerVendor;
use self::publisher::Publisher;
use self::service::Service;
use failure::{Error, ResultExt};
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use futures::channel::mpsc::channel;
use futures::prelude::TryFutureExt;

type Result<T> = std::result::Result<T, Error>;

const CHANNEL_BUFFER_SIZE: usize = 100;

#[fasync::run_singlethreaded]
async fn main() -> Result<()> {
    let (fidl_sink, fidl_stream) = channel(CHANNEL_BUFFER_SIZE);
    let fidl_server = ServicesServer::new()
        .add_service(Publisher::factory(fidl_sink.clone()))
        .add_service(ControllerVendor::factory(fidl_sink.clone()))
        .start()
        .context("Starting Media Session FIDL server.")?;

    await!(Service::new().serve(fidl_stream).try_join(fidl_server))?;
    Ok(())
}
