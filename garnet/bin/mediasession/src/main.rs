// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![recursion_limit = "256"]

#[macro_use]
mod log_error;
mod publisher;
mod registry;
mod service;
mod session;
mod subscriber;
#[cfg(test)]
mod test;

use self::publisher::Publisher;
use self::registry::Registry;
use self::service::Service;
use failure::{Error, ResultExt};
use fidl_fuchsia_mediasession::SessionEntry;
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::channel::mpsc::channel;
use futures::prelude::TryFutureExt;
use zx::AsHandleRef;

type Result<T> = std::result::Result<T, Error>;

const CHANNEL_BUFFER_SIZE: usize = 100;

/// This number should be forgiving. If we lower it we may want to build some
/// in-process staging area for changes so we can send them to clients that ACK
/// late. At 20 though, clients that don't ACK can't reasonably expect to be
/// accomodated.
const MAX_EVENTS_SENT_WITHOUT_ACK: usize = 20;

fn session_id_rights() -> zx::Rights {
    zx::Rights::DUPLICATE | zx::Rights::TRANSFER | zx::Rights::INSPECT
}

fn clone_session_id_handle(session_id_handle: &zx::Event) -> Result<zx::Event> {
    Ok(session_id_handle.as_handle_ref().duplicate(session_id_rights())?.into())
}

fn clone_session_entry(entry: &SessionEntry) -> Result<SessionEntry> {
    Ok(SessionEntry {
        session_id: entry.session_id.as_ref().map(clone_session_id_handle).transpose()?,
        local: entry.local.clone(),
    })
}

#[fasync::run_singlethreaded]
async fn main() -> Result<()> {
    let (fidl_sink, fidl_stream) = channel(CHANNEL_BUFFER_SIZE);
    let fidl_server = ServicesServer::new()
        .add_service(Publisher::factory(fidl_sink.clone()))
        .add_service(Registry::factory(fidl_sink.clone()))
        .start()
        .context("Starting Media Session FIDL server.")?;

    await!(Service::new().serve(fidl_stream).try_join(fidl_server))?;
    Ok(())
}
