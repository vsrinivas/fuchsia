// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of fuchsia.net.stack.Stack.

use {
    crate::eventloop::Event,
    failure::Error,
    fidl::endpoints::{RequestStream, ServiceMarker},
    fidl_fuchsia_net::{SocketProviderMarker, SocketProviderRequestStream},
    fidl_fuchsia_net_stack::{StackMarker, StackRequestStream},
    fuchsia_app::server::ServicesServer,
    fuchsia_async as fasync,
    futures::{channel::mpsc, TryFutureExt, TryStreamExt},
    log::error,
};

pub struct FidlWorker;

impl FidlWorker {
    pub fn spawn(self, event_chan: mpsc::UnboundedSender<Event>) -> Result<(), Error> {
        let stack_event_chan = event_chan.clone();
        let socket_event_chan = event_chan.clone();
        {
            fasync::spawn_local(
                ServicesServer::new()
                    .add_service((StackMarker::NAME, move |chan| {
                        Self::spawn_stack(chan, stack_event_chan.clone())
                    }))
                    .add_service((SocketProviderMarker::NAME, move |chan| {
                        Self::spawn_socket_provider(chan, socket_event_chan.clone())
                    }))
                    .start()?
                    .unwrap_or_else(|e: Error| error!("{:?}", e)),
            );
        }
        Ok(())
    }

    fn spawn_stack(chan: fasync::Channel, event_chan: mpsc::UnboundedSender<Event>) {
        fasync::spawn_local(
            async move {
                let mut stream = StackRequestStream::from_channel(chan);
                while let Some(req) = await!(stream.try_next())? {
                    event_chan.unbounded_send(Event::FidlStackEvent(req))?;
                }
                Ok(())
            }
                .unwrap_or_else(|e: Error| error!("{:?}", e)),
        );
    }

    fn spawn_socket_provider(chan: fasync::Channel, event_chan: mpsc::UnboundedSender<Event>) {
        fasync::spawn_local(
            async move {
                let mut stream = SocketProviderRequestStream::from_channel(chan);
                while let Some(req) = await!(stream.try_next())? {
                    event_chan.unbounded_send(Event::FidlSocketProviderEvent(req))?;
                }
                Ok(())
            }
                .unwrap_or_else(|e: Error| error!("{:?}", e)),
        );
    }
}
