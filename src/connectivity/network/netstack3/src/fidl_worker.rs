// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of fuchsia.net.stack.Stack.

use {
    crate::eventloop::Event,
    anyhow::Error,
    fidl_fuchsia_net_icmp::ProviderRequestStream as IcmpProviderRequestStream,
    fidl_fuchsia_net_stack::StackRequestStream,
    fidl_fuchsia_posix_socket::ProviderRequestStream as SocketProviderRequestStream,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{channel::mpsc, FutureExt, SinkExt, StreamExt, TryStreamExt},
    log::error,
};

pub struct FidlWorker;

impl FidlWorker {
    pub fn spawn(self, event_chan: mpsc::UnboundedSender<Event>) -> Result<(), Error> {
        let mut fs = ServiceFs::new_local();
        fs.dir("svc")
            .add_fidl_service(|rs: IcmpProviderRequestStream| {
                rs.map_ok(Event::FidlIcmpProviderEvent).left_stream()
            })
            .add_fidl_service(|rs: StackRequestStream| {
                rs.map_ok(Event::FidlStackEvent).left_stream().right_stream()
            })
            .add_fidl_service(|rs: SocketProviderRequestStream| {
                rs.map_ok(Event::FidlSocketProviderEvent).right_stream().right_stream()
            });
        fs.take_and_serve_directory_handle()?;

        fasync::spawn_local(async move {
            while let Some(event_stream) = fs.next().await {
                let event_chan = event_chan.clone().sink_map_err(|e| error!("{:?}", e));
                let event_stream = event_stream.map_err(|e| error!("{:?}", e));
                fasync::spawn_local(
                    event_stream.forward(event_chan).map(|_sink_res: Result<_, ()>| ()),
                );
            }
        });

        Ok(())
    }
}
