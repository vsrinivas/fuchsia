// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::event::Event,
    anyhow::Error,
    fidl_fuchsia_net_stack as stack, fidl_fuchsia_netstack as netstack, fuchsia_async as fasync,
    futures::{channel::mpsc, StreamExt, TryFutureExt},
};

/// `EventWorker` is a worker to process events.
pub(crate) struct EventWorker;

impl EventWorker {
    /// `spawn` waits for netstack and stack events, converts them into `eventloop::Event` and
    /// sends them on `event_channel`.
    pub fn spawn(
        self,
        streams: (stack::StackEventStream, netstack::NetstackEventStream),
        event_chan: mpsc::UnboundedSender<Event>,
    ) {
        fasync::spawn_local(
            async move {
                let mut select_stream = futures::stream::select(
                    streams.0.map(|e| e.map(Event::StackEvent)),
                    streams.1.map(|e| e.map(Event::NetstackEvent)),
                );

                while let Some(e) = select_stream.next().await {
                    info!("Received event: {:?}", e);
                    match e {
                        Ok(e) => event_chan.unbounded_send(e)?,
                        Err(e) => error!("Fidl event error: {}", e),
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|err: Error| error!("Received event error {:?}", err)),
        );
    }
}
