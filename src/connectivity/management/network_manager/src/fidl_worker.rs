// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of fuchsia.router.config

use {
    crate::event::Event,
    anyhow::Error,
    fidl_fuchsia_router_config::{RouterAdminRequestStream, RouterStateRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{channel::mpsc, StreamExt, TryFutureExt, TryStreamExt},
    log::error,
};

pub struct FidlWorker;

impl FidlWorker {
    pub fn spawn(self, event_chan: mpsc::UnboundedSender<Event>) -> Result<(), Error> {
        let router_admin_event_chan = event_chan.clone();
        let router_state_event_chan = event_chan;

        let mut fs = ServiceFs::new_local();
        fs.dir("svc")
            .add_fidl_service(move |rs: RouterAdminRequestStream| {
                Self::spawn_router_admin(rs, router_admin_event_chan.clone())
            })
            .add_fidl_service(move |rs: RouterStateRequestStream| {
                Self::spawn_router_state(rs, router_state_event_chan.clone())
            });
        fs.take_and_serve_directory_handle()?;
        fasync::spawn_local(fs.collect());
        Ok(())
    }

    pub fn spawn_router_admin(
        mut stream: RouterAdminRequestStream,
        event_chan: mpsc::UnboundedSender<Event>,
    ) {
        fasync::spawn_local(
            async move {
                while let Some(req) = stream.try_next().await? {
                    event_chan.unbounded_send(Event::FidlRouterAdminEvent(req))?;
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| error!("{:?}", e)),
        );
    }

    pub fn spawn_router_state(
        mut stream: RouterStateRequestStream,
        event_chan: mpsc::UnboundedSender<Event>,
    ) {
        fasync::spawn_local(
            async move {
                while let Some(req) = stream.try_next().await? {
                    event_chan.unbounded_send(Event::FidlRouterStateEvent(req))?;
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| error!("{:?}", e)),
        );
    }
}
