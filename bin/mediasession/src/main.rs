// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

mod publisher;
mod service;
mod session;

use self::publisher::Publisher;
use self::service::Service;
use failure::{Error, ResultExt};
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use futures::channel::mpsc::channel;
use futures::prelude::TryFutureExt;

const CHANNEL_BUFFER_SIZE: usize = 100;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let (new_session_sender, new_session_receiver) = channel(CHANNEL_BUFFER_SIZE);
    let fidl_server = ServicesServer::new()
        .add_service(Publisher::factory(new_session_sender))
        .start()
        .context("Starting Media Session FIDL server.")?;

    let service = Service::new(new_session_receiver);

    await!(service.serve().try_join(fidl_server))?;
    Ok(())
}
