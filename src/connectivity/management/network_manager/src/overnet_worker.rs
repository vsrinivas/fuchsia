// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides overnet support for network manager.

use {
    crate::event::Event,
    crate::fidl_worker::FidlWorker,
    anyhow::Error,
    fidl::endpoints::{create_request_stream, RequestStream, ServiceMarker},
    fidl_fuchsia_overnet::{
        ServiceProviderMarker, ServiceProviderRequest, ServicePublisherMarker,
        ServicePublisherProxy,
    },
    fidl_fuchsia_router_config::{
        RouterAdminMarker, RouterAdminRequestStream, RouterStateMarker, RouterStateRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_component::client as fclient,
    futures::channel::mpsc,
    futures::TryStreamExt,
};

pub struct OvernetWorker;

impl OvernetWorker {
    pub fn spawn(self, event_ch: mpsc::UnboundedSender<Event>) -> Result<(), Error> {
        let admin_event_ch = event_ch.clone();
        let state_event_ch = event_ch;

        let overnet_svc = fclient::connect_to_service::<ServicePublisherMarker>()?;
        Self::setup_overnet_service(&overnet_svc, &RouterAdminMarker::NAME, move |ch| {
            FidlWorker::spawn_router_admin(
                RouterAdminRequestStream::from_channel(ch),
                admin_event_ch.clone(),
            )
        })?;
        Self::setup_overnet_service(&overnet_svc, &RouterStateMarker::NAME, move |ch| {
            FidlWorker::spawn_router_state(
                RouterStateRequestStream::from_channel(ch),
                state_event_ch.clone(),
            )
        })?;

        Ok(())
    }

    fn setup_overnet_service<F: 'static>(
        overnet: &ServicePublisherProxy,
        name: &str,
        callback: F,
    ) -> Result<(), Error>
    where
        F: Fn(fasync::Channel),
    {
        let (client, mut server) = create_request_stream::<ServiceProviderMarker>()?;
        overnet.publish_service(name, client)?;
        fasync::spawn_local(async move {
            while let Some(ServiceProviderRequest::ConnectToService {
                chan: ch_req,
                control_handle: _control_handle,
                ..
            }) = server.try_next().await.unwrap()
            {
                callback(fasync::Channel::from_channel(ch_req).unwrap());
            }
        });
        Ok(())
    }
}
