// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of fuchsia.net.stack.Stack.

use {
    crate::eventloop::Event,
    failure::Error,
    fidl::endpoints::{RequestStream, ServiceMarker},
    fidl_fuchsia_net_stack::{StackMarker, StackRequestStream},
    fuchsia_app::server::ServicesServer,
    fuchsia_async as fasync,
    futures::{channel::mpsc, TryFutureExt, TryStreamExt},
    log::error,
};

pub struct FidlServer;

impl FidlServer {
    pub fn spawn(self, event_chan: mpsc::UnboundedSender<Event>) -> Result<(), Error> {
        fasync::spawn_local(
            ServicesServer::new()
                .add_service((StackMarker::NAME, move |chan| {
                    self.spawn_stack(chan, event_chan.clone())
                }))
                .start()?
                .unwrap_or_else(|e: Error| error!("{:?}", e)),
        );
        Ok(())
    }

    fn spawn_stack(&self, chan: fasync::Channel, event_chan: mpsc::UnboundedSender<Event>) {
        fasync::spawn_local(
            async move {
                let mut stream = StackRequestStream::from_channel(chan);
                while let Some(req) = await!(stream.try_next())? {
                    event_chan.unbounded_send(Event::FidlEvent(req))?;
                }
                Ok(())
            }
                .unwrap_or_else(|e: Error| error!("{:?}", e)),
        );
    }
}
