// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![recursion_limit = "512"]

#[macro_use]
mod log_error;
mod active_session_queue;
mod fidl_clones;
mod mpmc;
mod publisher;
mod registry;
mod session_list;
mod session_proxy;
mod subscriber;
#[cfg(test)]
mod test;

use self::{
    active_session_queue::ActiveSessionQueue, publisher::Publisher, registry::Registry,
    session_list::SessionList,
};
use failure::{Error, ResultExt};
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::lock::Mutex;
use std::sync::Arc;
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

#[fasync::run_singlethreaded]
async fn main() -> Result<()> {
    let session_list = Arc::new(Mutex::new(SessionList::default()));
    let active_session_queue = Arc::new(Mutex::new(ActiveSessionQueue::default()));
    let (active_session_sink, active_session_stream) = mpmc::channel(CHANNEL_BUFFER_SIZE);
    let (collection_event_sink, collection_event_stream) = mpmc::channel(CHANNEL_BUFFER_SIZE);

    let fidl_server = ServicesServer::new()
        .add_service(Publisher::factory(
            session_list.clone(),
            active_session_queue.clone(),
            active_session_sink,
            collection_event_sink,
        ))
        .add_service(Registry::factory(
            session_list.clone(),
            active_session_queue.clone(),
            active_session_stream,
            collection_event_stream,
        ))
        .start()
        .context("Starting Media Session FIDL server.")?;

    await!(fidl_server)?;
    Ok(())
}
